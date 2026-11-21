import copy
import json
import os
import threading
import unittest
import unittest.mock
import tempfile
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import engine_release
import harness_release
from harness_release import MAC_CHECK_STEP, MAC_JOB, ReleaseError, create_tag, find_release, finish_release, plan_phase, release_plan, validate_tag, verify_assets


SHA = "a" * 40
TAG = "openfallout-0.1.0-test"
RUN = {"id": 42, "head_branch": "master", "head_sha": SHA, "run_number": 1, "event": "push",
       "status": "completed", "conclusion": "success", "html_url": "https://example.invalid/run"}
ARM = "1" * 64  # the sha256 of the Apple Silicon image GitHub reports
OTHER = "3" * 64  # an image of the same name built elsewhere (an ad hoc local package)
IMAGE = 0  # the index of the disk image in RELEASE["assets"]


def note(name, result, mode, smoke, renderer, digest, chip):
    """A line as CI/check_mac_package.py --notes writes it."""
    return (f"- {name}: packaged-app check {result} ({mode} mode; smoke {smoke}; renderer {renderer}; sha256 {digest}) "
            f"on macOS 26.1 (25B78), {chip}, arm64. Apple Silicon (arm64), Developer ID signed and notarized, built "
            f"for macOS 26.0.")


RELEASE = {"id": 7, "tag_name": TAG, "draft": True, "prerelease": True,
           "body": note("OpenFallout-0.1.0-macOS-arm64.dmg", "PASS", "release", "SKIP", "not reported", ARM, "Apple M2 (Virtual)")
                   + "\n" + note("OpenFallout-0.1.0-macOS-arm64.dmg", "PASS", "release", "PASS", "Apple M4", ARM, "Apple M4") + "\n",
           "html_url": "https://example.invalid/release", "assets": [
               {"name": "OpenFallout-0.1.0-macOS-arm64.dmg", "size": 12, "state": "uploaded", "digest": "sha256:" + ARM}]}

# The engine release's statement (CI/engine_release.py) and the two notes lines the gate asks for. The image is the
# Apple Silicon one above; the commit is the one the tag points to.
STATEMENT = engine_release.build_statement("0.1.0", TAG, SHA, "OpenFallout-0.1.0-macOS-arm64.dmg", ARM, "26.0", "arm64")


def with_engine_release(release):
    """The release as the gate wants an engine release: the statement uploaded, the two notes lines present."""
    release["body"] += (engine_release.supported_line("26.0") + "\n"
                        + engine_release.played_on_line("MacBook Pro, Apple M9", "27.2") + "\n")
    release["assets"].append({"id": 99, "name": engine_release.STATEMENT_NAME, "size": 300, "state": "uploaded",
                              "digest": "sha256:" + "5" * 64})
    return release



def mac_job(name, conclusion="success"):
    """A job as the jobs API returns it, with the step the release gate reads."""
    return {"name": name, "conclusion": conclusion, "steps": [{"name": MAC_CHECK_STEP, "conclusion": conclusion}]}

class FakeGitHub:
    def __init__(self):
        self.statement = copy.deepcopy(STATEMENT)
        self.downloads = []
        self.writes = []
        self.comparison = "identical"
        self.marker = {}
        self.builds = [copy.deepcopy(RUN)]
        self.ref = None
        self.packaging = dict(RUN, head_branch=TAG)
        self.release = with_engine_release(copy.deepcopy(RELEASE))
        self.release_pages = None
        self.master_sha = SHA
        self.jobs = [{"name": "Changes", "conclusion": "success"}, mac_job(MAC_JOB)]

    def download_asset(self, asset):
        self.downloads.append(asset["name"])
        if asset["name"] != engine_release.STATEMENT_NAME:
            raise AssertionError(asset["name"])
        return self.statement if isinstance(self.statement, bytes) else json.dumps(self.statement).encode()

    def request(self, path, method="GET", data=None, missing_ok=False):
        if method != "GET":
            self.writes.append((path, method, data))
            return dict(self.release, **data) if path.startswith("releases/") else {}
        if path.startswith("commits/"):
            ref = path.split("/", 1)[1]
            return {"sha": ref if len(ref) == 40 else self.master_sha}
        if path.startswith("compare/"):
            return {"status": self.comparison}
        if path.startswith("contents/"):
            return self.marker
        if path.startswith("actions/workflows/push.yml/"):
            return {"workflow_runs": self.builds}
        if path.startswith("git/ref/"):
            return self.ref
        if path.startswith("actions/workflows/release.yml/"):
            return {"workflow_runs": [self.packaging]}
        if path.startswith("actions/runs/"):
            return {"jobs": self.jobs}
        if path.startswith("releases?"):
            if self.release_pages is not None:
                return self.release_pages[int(path.rsplit("=", 1)[1]) - 1]
            return [self.release]
        raise AssertionError(path)


class ReleaseTests(unittest.TestCase):
    def test_preview_is_read_only(self):
        api = FakeGitHub()
        self.assertTrue(release_plan(api, "master", TAG)["ready"])
        self.assertEqual(api.writes, [])

    def test_bad_tags(self):
        for tag in ("v1.0", "openfallout-../bad", "openfallout-a..b", "openfallout-a.lock", "openfallout-a."):
            with self.subTest(tag=tag), self.assertRaises(ReleaseError):
                validate_tag(tag)

    def test_unmerged_source_or_missing_setup_blocks_writes(self):
        for field, value in (("comparison", "ahead"), ("marker", None)):
            api = FakeGitHub()
            setattr(api, field, value)
            with self.assertRaises(ReleaseError):
                create_tag(api, release_plan(api, "feature", TAG))
            self.assertEqual(api.writes, [])

    def test_latest_failed_or_pending_build_blocks_release(self):
        for status, conclusion in (("completed", "failure"), ("in_progress", None)):
            api = FakeGitHub()
            api.builds.append(dict(RUN, run_number=2, status=status, conclusion=conclusion))
            with self.assertRaises(ReleaseError):
                create_tag(api, release_plan(api, "master", TAG))
            self.assertEqual(api.writes, [])

    def test_docs_only_success_with_a_skipped_mac_job_blocks_release(self):
        api = FakeGitHub()
        api.jobs[1]["conclusion"] = "skipped"
        with self.assertRaisesRegex(ReleaseError, "macOS / Build, test and package"):
            create_tag(api, release_plan(api, "master", TAG))
        self.assertEqual(api.writes, [])

    def test_docs_only_success_with_every_mac_step_skipped_blocks_release(self):
        # the split Mac jobs (macos.yml) decide for themselves: on a docs-only change they skip every step and succeed
        api = FakeGitHub()
        api.jobs[1]["steps"] = [{"name": "Decide whether the Mac build is needed", "conclusion": "success"},
                                {"name": MAC_CHECK_STEP, "conclusion": "skipped"}]
        with self.assertRaisesRegex(ReleaseError, "macOS / Build, test and package"):
            create_tag(api, release_plan(api, "master", TAG))
        self.assertEqual(api.writes, [])

    def test_the_gate_step_is_a_step_of_the_mac_job(self):
        macos = (Path(__file__).resolve().parent.parent / ".github/workflows/macos.yml").read_text()
        self.assertIn(f"- name: {MAC_CHECK_STEP}\n", macos)

    def test_the_one_mac_job_is_the_only_job_the_gate_waits_for(self):
        # no Ubuntu, Windows or Intel Mac job is expected: a run that has only the Mac job qualifies
        api = FakeGitHub()
        api.jobs = [mac_job(MAC_JOB)]
        self.assertTrue(release_plan(api, "master", TAG)["ready"])
        for job in ("Ubuntu", "Windows (2025) / windows-2025", "MacOS (macos-26-intel) / macos-26-intel",
                    "MacOS (macos-26) / macos-26"):
            api.jobs = [mac_job(job)]
            with self.subTest(job=job):
                self.assertFalse(release_plan(api, "master", TAG)["ready"])

    def test_the_check_name_is_the_one_the_workflows_give_the_mac_job(self):
        # push.yml names the calling job, macos.yml the job it calls; GitHub joins them with " / "
        workflows = Path(__file__).resolve().parent.parent / ".github/workflows"
        caller, called = MAC_JOB.split(" / ")
        push = (workflows / "push.yml").read_text()
        self.assertIn(f"  MacOS:\n    name: {caller}\n", push)
        self.assertIn("uses: ./.github/workflows/macos.yml", push)
        self.assertIn(f"  MacOS:\n    # A fixed name", (workflows / "macos.yml").read_text())
        self.assertIn(f"\n    name: {called}\n", (workflows / "macos.yml").read_text())
        self.assertNotIn("matrix", push)

    def test_full_manual_master_build_can_qualify_a_docs_only_commit(self):
        api = FakeGitHub()
        api.builds = [dict(RUN, event="workflow_dispatch")]
        self.assertTrue(release_plan(api, "master", TAG)["ready"])

    def test_create_tag_once_and_never_overwrite(self):
        api = FakeGitHub()
        create_tag(api, release_plan(api, "master", TAG))
        self.assertEqual(api.writes, [("git/refs", "POST", {"ref": f"refs/tags/{TAG}", "sha": SHA})])
        api.writes.clear()
        api.ref = {"object": {"type": "commit", "sha": SHA}}
        create_tag(api, release_plan(api, "master", TAG))
        self.assertEqual(api.writes, [])
        api.ref["object"]["sha"] = "b" * 40
        with self.assertRaises(ReleaseError):
            create_tag(api, release_plan(api, "master", TAG))
        self.assertEqual(api.writes, [])
        api.ref = {"object": {"type": "tag", "sha": SHA}}
        with self.assertRaises(ReleaseError):
            create_tag(api, release_plan(api, "master", TAG))
        self.assertEqual(api.writes, [])

    def test_missing_empty_or_wrong_architecture_image_blocks_publish(self):
        release = copy.deepcopy(RELEASE)
        release["assets"][IMAGE]["size"] = 0
        with self.assertRaisesRegex(ReleaseError, "Apple Silicon disk image"):
            verify_assets(release)
        release = copy.deepcopy(RELEASE)
        release["assets"] = []
        with self.assertRaisesRegex(ReleaseError, "Apple Silicon disk image"):
            verify_assets(release)
        for name in ("OpenFallout-unknown.dmg", "OpenFallout-x86_64.dmg", "OpenFallout-universal.dmg"):
            release = copy.deepcopy(RELEASE)
            release["assets"][IMAGE]["name"] = name
            with self.subTest(name=name), self.assertRaises(ReleaseError):
                verify_assets(release)

    def test_an_image_or_installer_of_another_platform_blocks_publish(self):
        # one disk image, for Apple Silicon: an Intel image, a second image or a Windows or Linux package is refused
        for name in ("OpenFallout-x86_64.dmg", "OpenFallout-arm64-2.dmg", "OpenFallout.exe", "OpenFallout.msi",
                     "OpenFallout-x86_64.AppImage", "openfallout_0.1.0_amd64.deb"):
            release = copy.deepcopy(RELEASE)
            release["assets"].append({"name": name, "size": 12, "state": "uploaded", "digest": "sha256:" + "4" * 64})
            release["body"] += note(name, "PASS", "release", "PASS", "Apple M4", "4" * 64, "Apple M4") + "\n"
            with self.subTest(name=name), self.assertRaisesRegex(ReleaseError, name):
                verify_assets(release)

    def test_disk_image_without_its_checked_on_line_blocks_publish(self):
        release = copy.deepcopy(RELEASE)
        release["body"] = None
        with self.assertRaisesRegex(ReleaseError, "macOS version and chip that OpenFallout-0.1.0-macOS-arm64.dmg"):
            verify_assets(release)

    def test_a_checked_on_line_needs_both_the_macos_version_and_the_chip(self):
        runner, gpu = RELEASE["body"].splitlines()
        for broken in (gpu.replace(", Apple M4, arm64.", "."),
                       gpu.replace("Apple M4, arm64", "unknown chip, arm64"),
                       gpu.replace("on macOS 26.1 (25B78)", "on macOS ? (?)"),
                       "- OpenFallout-0.1.0-macOS-arm64.dmg: checked on macOS 26.1"):
            release = copy.deepcopy(RELEASE)
            release["body"] = broken
            with self.subTest(line=broken), self.assertRaisesRegex(ReleaseError, "OpenFallout-0.1.0-macOS-arm64.dmg"):
                verify_assets(release)

    def test_arm64_image_needs_a_release_mode_pass_and_a_smoke_pass(self):
        release = copy.deepcopy(RELEASE)
        verify_assets(release)
        runner, gpu = release["body"].splitlines()
        # only the runner's line: the game was never started on a Mac with a GPU
        release["body"] = runner
        with self.assertRaisesRegex(ReleaseError, "smoke PASS.*OpenFallout-0.1.0-macOS-arm64.dmg"):
            verify_assets(release)
        # only a smoke run in local mode: the notarized image was never checked in release mode
        release["body"] = gpu.replace("release mode", "local mode")
        with self.assertRaisesRegex(ReleaseError, "release mode.*OpenFallout-0.1.0-macOS-arm64.dmg"):
            verify_assets(release)
        # a failed check does not count, whatever its smoke result
        release["body"] = "\n".join([runner, gpu.replace("check PASS", "check FAIL")])
        with self.assertRaises(ReleaseError):
            verify_assets(release)
        # one line with both is enough
        release["body"] = gpu
        verify_assets(release)

    def test_passes_count_only_for_the_image_that_was_uploaded(self):
        runner, gpu = RELEASE["body"].splitlines()
        release = copy.deepcopy(RELEASE)
        # the smoke run was of another image with the same name (an ad hoc local package)
        release["body"] = "\n".join([runner, gpu.replace(ARM, OTHER)])
        with self.assertRaisesRegex(ReleaseError, "smoke PASS.*OpenFallout-0.1.0-macOS-arm64.dmg"):
            verify_assets(release)
        # the release-mode check was of another image
        release["body"] = "\n".join([runner.replace(ARM, OTHER), gpu.replace("release mode", "local mode")])
        with self.assertRaisesRegex(ReleaseError, "release mode.*OpenFallout-0.1.0-macOS-arm64.dmg"):
            verify_assets(release)
        # GitHub gave no digest for the image: nothing can be bound to it
        release = copy.deepcopy(RELEASE)
        del release["assets"][IMAGE]["digest"]
        with self.assertRaisesRegex(ReleaseError, "digest.*OpenFallout-0.1.0-macOS-arm64.dmg"):
            verify_assets(release)

    def test_a_smoke_pass_on_the_software_renderer_does_not_count(self):
        runner, gpu = RELEASE["body"].splitlines()
        release = copy.deepcopy(RELEASE)
        for renderer in ("Apple Software Renderer", "not reported"):
            release["body"] = "\n".join([runner, gpu.replace("renderer Apple M4", f"renderer {renderer}")])
            with self.subTest(renderer=renderer), self.assertRaisesRegex(ReleaseError, "GPU.*OpenFallout-0.1.0-macOS-arm64.dmg"):
                verify_assets(release)

    def test_failed_packaging_blocks_publishing(self):
        api = FakeGitHub()
        api.packaging["conclusion"] = "failure"
        with self.assertRaises(ReleaseError):
            finish_release(api, release_plan(api, "master", TAG), publish=True)
        self.assertEqual(api.writes, [])

    def test_verify_keeps_draft_and_publish_only_promotes_prerelease(self):
        api = FakeGitHub()
        plan = release_plan(api, "master", TAG)
        finish_release(api, plan)
        self.assertEqual(api.writes, [])
        finish_release(api, plan, publish=True)
        self.assertEqual(api.writes, [("releases/7", "PATCH", {"draft": False, "prerelease": True})])

    def test_draft_lookup_paginates_and_missing_release_fails(self):
        api = FakeGitHub()
        api.release_pages = [[dict(RELEASE, tag_name=f"other-{i}") for i in range(100)], [RELEASE]]
        self.assertEqual(find_release(api, TAG)["id"], 7)
        api.release_pages = [[]]
        with self.assertRaises(ReleaseError):
            find_release(api, TAG)

    def test_master_advancing_does_not_change_source_between_phases(self):
        api = FakeGitHub()
        with tempfile.TemporaryDirectory() as directory:
            state_file = Path(directory) / "source.json"
            preview = plan_phase(api, "preview", "master", TAG, state_file)
            api.master_sha = "b" * 40
            api.ref = {"object": {"type": "commit", "sha": SHA}}
            for phase in ("tag", "wait", "verify", "publish"):
                plan = plan_phase(api, phase, "master", TAG, state_file)
                self.assertEqual(plan["sha"], preview["sha"])
                self.assertTrue(plan["ready"])
            self.assertEqual(api.writes, [])

    def test_later_phases_reject_missing_or_mismatched_state(self):
        with tempfile.TemporaryDirectory() as directory:
            state_file = Path(directory) / "source.json"
            api = FakeGitHub()
            with self.assertRaises(ReleaseError):
                plan_phase(api, "tag", "master", TAG, state_file)
            plan_phase(api, "preview", "master", TAG, state_file)
            with self.assertRaises(ReleaseError):
                plan_phase(api, "publish", "master", "openfallout-other", state_file)


class EngineReleaseGateTests(unittest.TestCase):
    """The gate refuses to publish an engine release that lacks its statement, whose statement disagrees with the
    uploaded image or the tag, or whose notes lack the supported or the played-on line."""

    def refused(self, api, pattern):
        plan = release_plan(api, "master", TAG)
        for publish in (False, True):
            with self.subTest(publish=publish), self.assertRaisesRegex(ReleaseError, pattern):
                finish_release(api, plan, publish=publish)
        self.assertEqual(api.writes, [])

    def test_a_release_with_its_statement_and_its_notes_publishes(self):
        api = FakeGitHub()
        finish_release(api, release_plan(api, "master", TAG), publish=True)
        self.assertEqual(api.writes, [("releases/7", "PATCH", {"draft": False, "prerelease": True})])
        self.assertEqual(api.downloads, [engine_release.STATEMENT_NAME] * 1)

    def test_a_release_without_the_statement_is_refused(self):
        api = FakeGitHub()
        api.release["assets"] = [asset for asset in api.release["assets"] if asset["name"] != engine_release.STATEMENT_NAME]
        self.refused(api, engine_release.STATEMENT_NAME)

    def test_a_statement_that_was_not_uploaded_completely_is_refused(self):
        api = FakeGitHub()
        api.release["assets"][-1]["size"] = 0
        self.refused(api, engine_release.STATEMENT_NAME)

    def test_a_statement_that_is_not_json_or_not_a_statement_is_refused(self):
        for content in (b"{broken", b'["a"]', json.dumps({"schema": 1}).encode()):
            api = FakeGitHub()
            api.statement = content
            with self.subTest(content=content):
                self.refused(api, engine_release.STATEMENT_NAME)

    def test_a_statement_with_another_digest_than_the_uploaded_image_is_refused(self):
        api = FakeGitHub()
        api.statement["sha256"] = OTHER
        self.refused(api, f"sha256 {OTHER}.*OpenFallout-0.1.0-macOS-arm64.dmg.*{ARM}")

    def test_an_image_uploaded_again_after_the_statement_was_made_is_refused(self):
        api = FakeGitHub()
        api.release["assets"][IMAGE]["digest"] = "sha256:" + OTHER
        # the notes' check lines were bound to the first image, so that refusal comes first: the statement must be
        # refused for the same reason once they are bound to the new one
        api.release["body"] = api.release["body"].replace(ARM, OTHER)
        self.refused(api, f"sha256 {ARM}.*{OTHER}")

    def test_a_statement_without_a_digest_for_its_image_on_github_is_refused(self):
        api = FakeGitHub()
        del api.release["assets"][IMAGE]["digest"]
        self.refused(api, "digest")

    def test_a_statement_of_another_source_commit_than_the_tags_is_refused(self):
        api = FakeGitHub()
        api.statement["source_commit"] = "b" * 40
        self.refused(api, f"built from {'b' * 40}.*{SHA}")

    def test_a_statement_of_another_tag_than_the_releases_is_refused(self):
        api = FakeGitHub()
        api.statement["tag"] = "openfallout-0.1.0-other"
        self.refused(api, "tag openfallout-0.1.0-other.*" + TAG)

    def test_a_statement_that_names_an_image_the_release_does_not_hold_is_refused(self):
        api = FakeGitHub()
        api.release["assets"][IMAGE]["name"] = "OpenFallout-arm64.dmg"
        api.release["body"] = api.release["body"].replace("OpenFallout-0.1.0-macOS-arm64.dmg", "OpenFallout-arm64.dmg")
        self.refused(api, "OpenFallout-0.1.0-macOS-arm64.dmg.*does not hold")

    def test_an_image_that_is_not_at_the_stable_address_of_its_version_is_refused(self):
        api = FakeGitHub()
        api.release["assets"][IMAGE]["name"] = "OpenFallout-arm64.dmg"
        api.release["body"] = api.release["body"].replace("OpenFallout-0.1.0-macOS-arm64.dmg", "OpenFallout-arm64.dmg")
        api.statement["disk_image"] = "OpenFallout-arm64.dmg"
        self.refused(api, "OpenFallout-arm64.dmg.*is named OpenFallout-0.1.0-macOS-arm64.dmg")

    def test_notes_without_the_supported_line_are_refused(self):
        api = FakeGitHub()
        api.release["body"] = "\n".join(line for line in api.release["body"].splitlines() if "Supported" not in line)
        self.refused(api, "Supported")

    def test_notes_with_another_minimum_than_the_statements_are_refused(self):
        api = FakeGitHub()
        api.release["body"] = api.release["body"].replace("macOS 26.0 or later", "macOS 14.8 or later")
        self.refused(api, "Supported.*26.0")

    def test_notes_without_the_played_on_line_are_refused(self):
        api = FakeGitHub()
        api.release["body"] = "\n".join(line for line in api.release["body"].splitlines() if "Played on" not in line)
        self.refused(api, "Played on")

    def test_a_played_on_line_that_names_no_macos_version_is_refused(self):
        api = FakeGitHub()
        api.release["body"] = api.release["body"].replace("MacBook Pro, Apple M9, macOS 27.2", "a Mac")
        self.refused(api, "Played on")


class AssetDownloadTests(unittest.TestCase):
    """GitHub's asset download answers with a redirect to a signed address on another host: the token goes to GitHub
    only and never to that host."""

    def test_the_token_is_sent_to_the_api_and_not_to_the_host_it_redirects_to(self):
        seen = {}

        class Blob(BaseHTTPRequestHandler):
            def do_GET(self):
                seen["blob"] = self.headers.get("Authorization")
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b'{"schema": 1}')

            def log_message(self, *args):
                pass

        blob = HTTPServer(("127.0.0.1", 0), Blob)

        class Api(BaseHTTPRequestHandler):
            def do_GET(self):
                seen["api"] = (self.headers.get("Authorization"), self.headers.get("Accept"))
                self.send_response(302)
                self.send_header("Location", f"http://127.0.0.1:{blob.server_port}/signed")
                self.end_headers()

            def log_message(self, *args):
                pass

        api = HTTPServer(("127.0.0.1", 0), Api)
        for server in (blob, api):
            threading.Thread(target=server.serve_forever, daemon=True).start()
            self.addCleanup(server.shutdown)
        with unittest.mock.patch.dict(os.environ, {"GH_TOKEN": "secret-token"}):
            content = harness_release.GitHub().download_asset({"name": "x", "url": f"http://127.0.0.1:{api.server_port}/asset"})
        self.assertEqual(content, b'{"schema": 1}')
        self.assertEqual(seen["api"], ("Bearer secret-token", "application/octet-stream"))
        self.assertIsNone(seen["blob"])

    def test_a_huge_or_failing_download_is_refused(self):
        class Huge(BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(200 if self.path == "/huge" else 404)
                self.end_headers()
                if self.path == "/huge":
                    self.wfile.write(b"x" * (harness_release.MAX_STATEMENT_BYTES + 10))

            def log_message(self, *args):
                pass

        server = HTTPServer(("127.0.0.1", 0), Huge)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.shutdown)
        with unittest.mock.patch.dict(os.environ, {"GH_TOKEN": "secret-token"}):
            for path in ("huge", "missing"):
                with self.subTest(path=path), self.assertRaises(ReleaseError):
                    harness_release.GitHub().download_asset({"name": "x", "url": f"http://127.0.0.1:{server.server_port}/{path}"})


if __name__ == "__main__":
    unittest.main()
