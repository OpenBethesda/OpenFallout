#!/usr/bin/env python3
"""Drive the desktop release workflow from a trusted Harness checkout."""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode, urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener, urlopen

import engine_release

REPOSITORY = "OpenBethesda/OpenFallout"
MARKER = ".harness/openfallout-release.yaml"
# The one job that builds the engine: push.yml's MacOS job ("macOS") calls macos.yml, whose job is "Build, test and
# package"; GitHub joins the two names. No other platform has a job.
MAC_JOB = "macOS / Build, test and package"
# A release holds one disk image, for Apple Silicon; these are the packages of other platforms.
OTHER_PLATFORM_SUFFIXES = (".exe", ".msi", ".appimage", ".deb", ".rpm", ".apk", ".flatpak", ".snap")


class ReleaseError(RuntimeError):
    pass


class GitHub:
    def __init__(self):
        self.token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
        if not self.token:
            raise ReleaseError("GH_TOKEN or GITHUB_TOKEN is required")

    def request(self, path, method="GET", data=None, missing_ok=False):
        request = Request(
            f"https://api.github.com/repos/{REPOSITORY}/{path}",
            data=json.dumps(data).encode() if data is not None else None,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Accept": "application/vnd.github+json",
                "X-GitHub-Api-Version": "2022-11-28",
                "Content-Type": "application/json",
            },
            method=method,
        )
        try:
            with urlopen(request, timeout=30) as response:
                return json.load(response)
        except HTTPError as error:
            if missing_ok and error.code == 404:
                return None
            raise ReleaseError(f"GitHub {method} {path.split('?')[0]} returned HTTP {error.code}") from None

    def download_asset(self, asset):
        """The bytes of an uploaded release asset (the statement of an engine release: a small file)."""
        request = Request(asset["url"], headers={
            "Authorization": f"Bearer {self.token}",
            "Accept": "application/octet-stream",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        try:
            with build_opener(SameHostRedirects).open(request, timeout=30) as response:
                content = response.read(MAX_STATEMENT_BYTES + 1)
        except HTTPError as error:
            raise ReleaseError(f"GitHub asset download of {asset['name']} returned HTTP {error.code}") from None
        except URLError as error:
            raise ReleaseError(f"GitHub asset download of {asset['name']} failed: {error.reason}") from None
        if len(content) > MAX_STATEMENT_BYTES:
            raise ReleaseError(f"{asset['name']} is larger than {MAX_STATEMENT_BYTES} bytes, which no statement is")
        return content


class SameHostRedirects(HTTPRedirectHandler):
    """GitHub answers an asset download with a redirect to a signed address on another host. The token goes to GitHub
    only: the new request to the other host carries none (that host refuses a request that has both)."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        redirected = super().redirect_request(req, fp, code, msg, headers, newurl)
        if redirected is not None and urlparse(newurl).netloc != urlparse(req.full_url).netloc:
            redirected.remove_header("Authorization")
        return redirected


MAX_STATEMENT_BYTES = 64 * 1024


def validate_tag(tag):
    if not re.fullmatch(r"openfallout-[A-Za-z0-9][A-Za-z0-9._-]{0,100}", tag):
        raise ReleaseError("Tag must start with openfallout- and contain only letters, numbers, dots, underscores or hyphens")
    if ".." in tag or tag.endswith((".", ".lock")):
        raise ReleaseError("Tag is not a valid Git reference")


def release_plan(api, source_ref, tag):
    validate_tag(tag)
    commit = api.request(f"commits/{quote(source_ref, safe='')}")
    sha = commit["sha"]
    comparison = api.request(f"compare/master...{sha}")
    problems = []
    if comparison["status"] not in ("identical", "behind"):
        problems.append("Source commit must be on master")
    marker = api.request(f"contents/{MARKER}?ref={sha}", missing_ok=True)
    if marker is None:
        problems.append("Merge the Harness release setup into master first")
    query = urlencode({"head_sha": sha, "per_page": 100})
    runs = api.request(f"actions/workflows/push.yml/runs?{query}")["workflow_runs"]
    latest = max((run for run in runs if run["head_branch"] == "master"
                  and run["event"] in ("push", "workflow_dispatch")),
                 key=lambda run: (run["run_number"], run.get("run_attempt", 1)), default=None)
    if not latest or latest["status"] != "completed" or latest["conclusion"] != "success":
        problems.append("The latest master Build and test run for this commit must pass")
    else:
        jobs = api.request(f"actions/runs/{latest['id']}/jobs?filter=latest&per_page=100")["jobs"]
        passed = {job["name"] for job in jobs if job["conclusion"] == "success"}
        if MAC_JOB not in passed:
            problems.append(f"The {MAC_JOB} job must pass; run the full Build and test workflow on this master commit")
    ref = api.request(f"git/ref/tags/{quote(tag, safe='')}", missing_ok=True)
    if ref and (ref["object"]["type"] != "commit" or ref["object"]["sha"] != sha):
        problems.append("Existing tag does not point directly to the selected commit; tags are never overwritten")
    return {"repository": REPOSITORY, "source_ref": source_ref, "sha": sha,
            "tag": tag, "ready": not problems, "problems": problems,
            "build_url": latest["html_url"] if latest else None, "tag_exists": bool(ref)}


def require_ready(plan):
    if not plan["ready"]:
        raise ReleaseError("; ".join(plan["problems"]))


def plan_phase(api, phase, source_ref, tag, state_file):
    if phase == "preview":
        plan = release_plan(api, source_ref, tag)
        state_file.write_text(json.dumps({"sha": plan["sha"], "source_ref": source_ref, "tag": tag}))
        return plan
    try:
        state = json.loads(state_file.read_text())
    except (OSError, ValueError):
        raise ReleaseError("Run the preview phase first to resolve and save the source commit") from None
    if (state.get("source_ref") != source_ref or state.get("tag") != tag
            or not re.fullmatch(r"[0-9a-f]{40}", state.get("sha", ""))):
        raise ReleaseError("Saved release source is invalid or belongs to different inputs")
    return release_plan(api, state["sha"], tag)


def create_tag(api, plan):
    require_ready(plan)
    if not plan["tag_exists"]:
        api.request("git/refs", "POST", {"ref": f"refs/tags/{plan['tag']}", "sha": plan["sha"]})
    print(f"Release tag ready: {plan['tag']} at {plan['sha']}")


def packaging_run(api, plan):
    query = urlencode({"head_sha": plan["sha"], "branch": plan["tag"], "event": "push", "per_page": 100})
    runs = api.request(f"actions/workflows/release.yml/runs?{query}")["workflow_runs"]
    matching = [run for run in runs if run["head_branch"] == plan["tag"] and run["head_sha"] == plan["sha"]]
    if not matching:
        raise ReleaseError("Release workflow has not registered yet; retry this Harness step")
    return max(matching, key=lambda run: (run["run_number"], run.get("run_attempt", 1)))


# The line CI/check_mac_package.py --notes writes for a disk image (write_notes): the result, the mode, the smoke result,
# the OpenGL renderer the smoke run reported, the image's SHA-256, then the macOS version (build) and chip it ran on.
NOTE_LINE = re.compile(
    r"^- (?P<name>[^:\s][^:]*): packaged-app check (?P<result>PASS|FAIL) "
    r"\((?P<mode>local|release) mode; smoke (?P<smoke>PASS|FAIL|SKIP|not run); renderer (?P<renderer>[^;()]+); "
    r"sha256 (?P<sha256>[0-9a-f]{64}|none)\) "
    r"on macOS (?P<macos>\d+(?:\.\d+)*) \([^()]*\), (?P<chip>[^,]+), (?P<arch>\w+)\.")
DIGEST = re.compile(r"sha256:([0-9a-f]{64})")


def note_lines(body):
    """The checked-on lines of the release notes that name a macOS version and a chip."""
    notes = []
    for line in (body or "").splitlines():
        found = NOTE_LINE.match(line.strip())
        if found and found["chip"].strip() not in ("", "unknown chip"):
            notes.append(found.groupdict())
    return notes


def gpu_renderer(renderer):
    """Whether the smoke run reported a renderer of a GPU (not Apple's software renderer, not none)."""
    renderer = renderer.strip().lower()
    return renderer != "not reported" and "software" not in renderer


def other_platform(name):
    """Whether an asset is a package for another platform than Apple Silicon Macs."""
    lowered = name.lower()
    return (lowered.endswith(OTHER_PLATFORM_SUFFIXES) or "x86_64" in lowered
            or (lowered.endswith(".dmg") and "arm64" not in lowered))


def verify_assets(release):
    uploaded = [asset for asset in release["assets"] if asset.get("size", 0) > 0 and asset.get("state") == "uploaded"]
    foreign = [asset["name"] for asset in uploaded if other_platform(asset["name"])]
    if foreign:
        raise ReleaseError(f"Release holds {', '.join(foreign)}, which is not for Apple Silicon Macs; a release has one "
                           f"disk image, for Apple Silicon")
    images = [asset for asset in uploaded if asset["name"].endswith(".dmg")]
    if not images:
        raise ReleaseError("Release is missing its Apple Silicon disk image")
    if len(images) > 1:
        raise ReleaseError(f"Release holds {len(images)} disk images ({', '.join(asset['name'] for asset in images)}); "
                           f"it has one, for Apple Silicon")
    # The disk image carries the line CI/check_mac_package.py --notes wrote: the macOS version and chip it was checked on.
    notes = note_lines(release.get("body"))
    for asset in images:
        name = asset["name"]
        mine = [note for note in notes if note["name"] == name]
        if not mine:
            raise ReleaseError(f"Release notes lack the macOS version and chip that {name} was checked on")
        # The notarized Apple Silicon image must have passed the check in release mode, and the game must have
        # started from it on a Mac with a GPU (a hosted runner has none; the run on such a Mac adds its line). Both
        # count only for the image that was uploaded: its SHA-256 as GitHub reports it.
        digest = DIGEST.fullmatch(asset.get("digest") or "")
        if not digest:
            raise ReleaseError(f"GitHub reports no sha256 digest for {name}, so no check can be bound to it")
        passed = [note for note in mine if note["result"] == "PASS" and note["sha256"] == digest.group(1)]
        if not any(note["mode"] == "release" for note in passed):
            raise ReleaseError(f"Release notes lack a packaged-app check PASS in release mode for {name} "
                               f"(sha256 {digest.group(1)})")
        if not any(note["smoke"] == "PASS" and gpu_renderer(note["renderer"]) for note in passed):
            raise ReleaseError(f"Release notes lack a smoke PASS (the game started on a Mac with a GPU renderer) for "
                               f"{name} (sha256 {digest.group(1)})")


def verify_engine_release(api, release, plan):
    """The engine release: its statement is uploaded, agrees with the uploaded image and the tag's commit, and the
    notes say what is supported and what the release was played on (CI/engine_release.py)."""
    try:
        engine_release.verify_release(release, plan["sha"], api.download_asset)
    except engine_release.Refusal as error:
        raise ReleaseError(str(error)) from None


def find_release(api, tag):
    # The tag endpoint only promises published releases. Authenticated listings
    # include drafts, including older drafts beyond the first page.
    page = 1
    while True:
        releases = api.request(f"releases?per_page=100&page={page}")
        matches = [release for release in releases if release["tag_name"] == tag]
        if matches:
            if len(matches) != 1:
                raise ReleaseError("More than one release matches the tag")
            return matches[0]
        if len(releases) < 100:
            raise ReleaseError("Packaging completed without a matching release")
        page += 1


def finish_release(api, plan, publish=False):
    require_ready(plan)
    run = packaging_run(api, plan)
    if run["status"] != "completed" or run["conclusion"] != "success":
        raise ReleaseError("Release packaging must finish successfully before verifying or publishing assets")
    release = find_release(api, plan["tag"])
    if release["tag_name"] != plan["tag"] or not release["prerelease"]:
        raise ReleaseError("Expected the selected tag's prerelease")
    verify_assets(release)
    verify_engine_release(api, release, plan)
    if publish and release["draft"]:
        release = api.request(f"releases/{release['id']}", "PATCH", {"draft": False, "prerelease": True})
    print(json.dumps({"url": release["html_url"], "draft": release["draft"], "prerelease": release["prerelease"]}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("preview", "tag", "wait", "verify", "publish"))
    parser.add_argument("--source-ref", default=os.environ.get("RELEASE_SOURCE", "master"))
    parser.add_argument("--tag", default=os.environ.get("RELEASE_TAG"), required=not os.environ.get("RELEASE_TAG"))
    parser.add_argument("--state-file", type=Path, default=Path(".harness-release-state.json"))
    args = parser.parse_args()
    api = GitHub()
    plan = plan_phase(api, args.phase, args.source_ref, args.tag, args.state_file)
    if args.phase == "preview":
        print(json.dumps(plan, indent=2))
    elif args.phase == "tag":
        create_tag(api, plan)
    elif args.phase == "wait":
        require_ready(plan)
        run = packaging_run(api, plan)
        print(f"Watching {run['html_url']}", flush=True)
        subprocess.run(["gh", "run", "watch", str(run["id"]), "--repo", REPOSITORY,
                        "--exit-status", "--interval", "30"], check=True)
    else:
        finish_release(api, plan, publish=args.phase == "publish")


if __name__ == "__main__":
    try:
        main()
    except (ReleaseError, subprocess.CalledProcessError) as error:
        print(f"Release stopped: {error}", file=sys.stderr)
        sys.exit(1)
