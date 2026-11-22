#!/usr/bin/env python3
"""Take OpenMW's changes into OpenFallout, one upstream merge request at a time, and keep a record of every one.

OpenFallout started as a copy of OpenMW and renamed paths and identifiers on 4 October 2026 (see
CI/upstream_translate.py) and deleted the parts it does not carry (the editor, the installation wizard, Windows and
Linux support). `git merge upstream/master` is therefore impossible, and nothing had been taken from upstream since.
This script does by machine what a person would do by hand with `git cherry-pick`: for every merge request upstream
merged after the fork point it translates the change into OpenFallout's names, applies it, and commits it with the
real author. A merge request that does not apply is not lost: the ledger, CI/upstream_ledger.jsonl, holds one line for
every upstream commit that was looked at, with the reason it was or was not taken, so the next run starts where the
last one ended and a person can see what still needs a hand port.

A unit of work is one commit on the first-parent line of the upstream branch after the fork point (upstream merges
every merge request with a merge commit, so a unit is normally one whole merge request). Its change is the diff
between its first parent and itself, restricted to the files the fork carries (CI/upstream_sync.json, dropped_paths).
That change is translated on both sides (the parent's files are the merge base, the unit's files are "theirs") and
merged onto our HEAD with git's own three-way merge; because the two small trees hold only the changed files,
everything else of ours is left alone. The outcome is one of:

    taken            the change merged cleanly and was committed (the commit holds the unit's ledger line)
    already-present  the merge changes nothing: the change is in our tree already
    not-applicable   every file of the unit is one the fork does not carry
    conflict         the merge needs a person; nothing was changed, and `pick` starts the hand port
    check-failed     the change merged but broke the --check-cmd (for example the build), so it was taken out again
    skipped          a person decided not to take it (`skip`)

A unit is also treated as taken when its `Upstream-Commit:` trailer is already in our history, ledger line or not.
Git's rename detection can move an upstream change into a similar file of ours when we deleted the original; such
a unit is taken but its ledger line and the report list the other files under "landed in another file".

Commit messages quote upstream text, so `#123` becomes "upstream issue 123", `!123` becomes a GitLab link and `@name`
loses its `@`: GitHub would otherwise close or notify OUR issues and people. The same cleaning applies to the report.

Usage (run from the repository, or give --repo DIR):
    python3 CI/upstream_sync.py status [--upstream REF] [--json]
    python3 CI/upstream_sync.py sync [--upstream REF] [--fetch] [--limit N] [--report FILE] [--summary FILE]
                                     [--clang-format PATH] [--check-cmd CMD]
    python3 CI/upstream_sync.py pick SHA        # merge one unit into the working tree and stop at the conflicts
    python3 CI/upstream_sync.py continue        # commit the resolved pick, ledger line "taken", note "ported by hand"
    python3 CI/upstream_sync.py abort           # undo a pick in progress
    python3 CI/upstream_sync.py skip SHA --reason TEXT

`status` changes nothing. `sync` needs a clean working tree on a branch and never fetches unless --fetch is given. The
exit status is 0 even when units conflict; it is non-zero only when the tool cannot work (dirty tree, missing ref, bad
configuration, a --check-cmd that already fails before the first unit).
"""

import argparse
import collections
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import upstream_translate

CONFIG_FILE = "CI/upstream_sync.json"
LEDGER_FILE = "CI/upstream_ledger.jsonl"
PICK_STATE_FILE = "upstream-sync-pick.json"  # inside .git, so it is never part of the tree
DECISIONS = ("taken", "already-present", "not-applicable", "conflict", "check-failed", "skipped")
# The folders and extensions our CI checks the formatting of (CI/check_format_changes.py).
FORMAT_FOLDERS = ("apps", "components")
FORMAT_EXTENSIONS = "c,cc,cpp,cxx,h,hpp"
GITLAB = "https://gitlab.com"
GITLAB_PROJECT = "OpenMW/openmw"
DEFAULT_COMMIT_URL = "https://github.com/OpenMW/openmw/commit/"
DELETED_NOTE = "upstream changed a file this fork deleted; add it to dropped_paths or port it by hand"
# Synthetic commits (the merge base B and the change T) get fixed identities so that the same unit always gives the
# same commit ids, and the objects are shared between runs.
SYNTHETIC = {
    "GIT_AUTHOR_NAME": "upstream-sync",
    "GIT_AUTHOR_EMAIL": "upstream-sync@invalid",
    "GIT_AUTHOR_DATE": "1000000000 +0000",
    "GIT_COMMITTER_NAME": "upstream-sync",
    "GIT_COMMITTER_EMAIL": "upstream-sync@invalid",
    "GIT_COMMITTER_DATE": "1000000000 +0000",
}


class ToolError(Exception):
    """A problem the person running the tool can fix; it is reported as one line, without a traceback."""


# ---------------------------------------------------------------------------------------------------------------------
# git


class Repo:
    """A git repository driven through the command line. Every call is an argument list, never a shell string."""

    def __init__(self, path):
        self.path = str(path)
        self._cat_file = None
        self._temp_dir = None

    def close(self):
        if self._cat_file is not None:
            self._cat_file.stdin.close()
            self._cat_file.wait()
            self._cat_file = None
        if self._temp_dir is not None:
            shutil.rmtree(self._temp_dir, ignore_errors=True)
            self._temp_dir = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def run(self, *args, input=None, env=None, check=True):
        """Run `git -C repo ARGS` and return the CompletedProcess (bytes). Paths are literal, never patterns."""
        full_env = dict(os.environ, GIT_LITERAL_PATHSPECS="1", **(env or {}))
        # A pager or a prompt in the middle of a script would hang a weekly job.
        full_env.update(GIT_PAGER="cat", GIT_TERMINAL_PROMPT="0")
        result = subprocess.run(["git", "-C", self.path, *args], input=input, capture_output=True, env=full_env)
        if check and result.returncode != 0:
            detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
            raise ToolError("git %s failed: %s" % (" ".join(args[:3]), detail))
        return result

    def out(self, *args, **kwargs):
        """The standard output of a git command as text without the trailing newline."""
        return self.run(*args, **kwargs).stdout.decode("utf-8", "surrogateescape").rstrip("\n")

    def resolve(self, ref):
        """The 40-hex id of a commit-ish, or None when there is no such commit."""
        result = self.run("rev-parse", "--verify", "--quiet", ref + "^{commit}", check=False)
        return result.stdout.decode().strip() or None

    def blob(self, sha):
        """The bytes of a blob, read through one long-lived `git cat-file --batch` process."""
        if self._cat_file is None:
            self._cat_file = subprocess.Popen(["git", "-C", self.path, "cat-file", "--batch"],
                                              stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self._cat_file.stdin.write(sha.encode() + b"\n")
        self._cat_file.stdin.flush()
        header = self._cat_file.stdout.readline().split()
        if len(header) != 3:
            raise ToolError("git cat-file cannot read the object " + sha)
        data = self._cat_file.stdout.read(int(header[2]))
        self._cat_file.stdout.read(1)
        return data

    def scratch(self):
        """A private temporary folder (for index files); removed by close()."""
        if self._temp_dir is None:
            self._temp_dir = tempfile.mkdtemp(prefix="upstream-sync-")
        return self._temp_dir

    def make_tree(self, entries):
        """Write a tree from (mode, sha, path) entries through a temporary index and return its id."""
        env = {"GIT_INDEX_FILE": os.path.join(self.scratch(), "index")}
        self.run("read-tree", "--empty", env=env)
        lines = "".join("%s %s\t%s\n" % (mode, sha, path) for mode, sha, path in entries)
        self.run("update-index", "--add", "--index-info", input=lines.encode("utf-8", "surrogateescape"), env=env)
        tree = self.out("write-tree", env=env)
        os.unlink(env["GIT_INDEX_FILE"])
        return tree

    def commit_tree(self, tree, parent=None, message="synthetic"):
        args = ["commit-tree", tree, "-m", message]
        if parent:
            args += ["-p", parent]
        return self.out(*args, env=SYNTHETIC)

    def is_clean(self):
        return not self.out("status", "--porcelain", "--untracked-files=no")

    def branch(self):
        """The checked-out branch, or None when HEAD is detached."""
        result = self.run("symbolic-ref", "--quiet", "--short", "HEAD", check=False)
        return result.stdout.decode().strip() or None

    def git_path(self, name):
        return self.out("rev-parse", "--path-format=absolute", "--git-path", name)


# ---------------------------------------------------------------------------------------------------------------------
# text taken from upstream


def sanitize(text):
    """Make upstream text safe to put in a commit message or a pull request: nothing in it may link or notify."""
    text = re.sub(r"(?m)^[ \t]*See merge request\b.*(?:\n|$)", "", text)
    # `!123` is a GitLab merge request, but GitHub would not link it; a URL keeps the pointer.
    text = re.sub(r"(?<![\w!])!(\d+)\b", lambda m: "%s/%s/-/merge_requests/%s" % (GITLAB, GITLAB_PROJECT, m.group(1)),
                  text)
    # `#123`, `owner/repo#123` and `GH-123` all link to (and "Closes" can close) an issue of THIS repository.
    text = re.sub(r"[\w.-]+/[\w.-]+#(\d+)\b", r"upstream issue \1", text)
    text = re.sub(r"(?<![\w&])#(\d+)\b", r"upstream issue \1", text)
    text = re.sub(r"\bGH-(\d+)\b", r"upstream issue \1", text)
    # `@name` notifies a person; an address like a@b.example has a word character before the `@` and is left alone.
    text = re.sub(r"(?<![\w@`])@(?=\w)", "", text)
    return text


def parse_message(raw, is_merge):
    """(title, description, merge request URL or None) of an upstream commit message, cleaned by sanitize().

    Upstream merge commits read "Merge branch 'x' into 'master'", a blank line, the merge request title, a blank line,
    the description, a blank line and "See merge request OpenMW/openmw!5794". Anything else is a plain commit message.
    """
    match = re.search(r"(?m)^[ \t]*See merge request (\S+)!(\d+)[ \t]*$", raw)
    url = "%s/%s/-/merge_requests/%s" % (GITLAB, match.group(1), match.group(2)) if match else None
    lines = sanitize(raw.replace("\r\n", "\n")).strip().split("\n")
    subject, body = lines[0], lines[1:]
    title, description = subject, body
    if is_merge and subject.startswith("Merge "):
        while body and not body[0].strip():
            body = body[1:]
        if body:
            title, description = body[0].strip(), body[1:]
    title = title.strip() or subject.strip()
    return title, "\n".join(description).strip(), url


# ---------------------------------------------------------------------------------------------------------------------
# configuration and ledger


def load_config(repo_path):
    path = Path(repo_path) / CONFIG_FILE
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ToolError("cannot read %s: %s" % (path, error.strerror))
    except ValueError as error:
        raise ToolError("%s is not valid JSON: %s" % (path, error))
    for key in ("upstream_url", "upstream_branch", "fork_point", "dropped_paths"):
        if key not in config:
            raise ToolError("%s has no \"%s\"" % (path, key))
    if not isinstance(config["dropped_paths"], list) or not all(isinstance(p, str) for p in config["dropped_paths"]):
        raise ToolError("%s: dropped_paths must be a list of strings" % path)
    return config


def is_dropped(path, patterns):
    """True when an upstream path matches a pattern: `folder/` means the whole folder, else fnmatch on the path."""
    for pattern in patterns:
        if pattern.endswith("/"):
            if path.startswith(pattern):
                return True
        elif fnmatch.fnmatchcase(path, pattern):
            return True
    return False


def read_ledger(repo_path):
    """{upstream sha: entry} in file order."""
    path = Path(repo_path) / LEDGER_FILE
    entries = {}
    if not path.exists():
        return entries
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            entry = json.loads(line)
            entries[entry["upstream"]] = entry
        except (ValueError, KeyError, TypeError):
            raise ToolError("%s line %d is not a ledger entry" % (path, number))
    return entries


def write_ledger(repo_path, entries, order):
    """Write the ledger in upstream order (entries of commits upstream no longer has keep their place at the top)."""
    path = Path(repo_path) / LEDGER_FILE
    ordered = sorted(entries.values(), key=lambda entry: order.get(entry["upstream"], -1))
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "".join(json.dumps(entry, sort_keys=True, ensure_ascii=False) + "\n" for entry in ordered)
    path.write_text(text, encoding="utf-8")


def make_entry(sha, title, decision, paths=None, note=None):
    """A ledger line; keys that would be empty are left out."""
    entry = {"upstream": sha, "title": title, "decision": decision}
    if paths:
        entry["paths"] = sorted(paths)
    if note:
        entry["note"] = note
    return entry


# ---------------------------------------------------------------------------------------------------------------------
# the engine


class Prepared:
    """The translated change of one unit: BASE and THEIRS as synthetic commits, and the paths it names."""

    def __init__(self, base, theirs, expected, modified):
        self.base = base
        self.theirs = theirs
        self.expected = expected  # translated paths of every non-dropped file of the unit
        self.modified = modified  # the part of `expected` that exists on both sides (a change, not an add or delete)


class Outcome:
    def __init__(self, decision, tree=None, paths=(), note=None, other=(), changed=(), files=0):
        self.decision = decision
        self.files = files  # how many files of the unit the fork carries
        self.tree = tree
        self.paths = list(paths)  # conflicted paths
        self.note = note
        self.other = list(other)  # paths changed that the unit does not name (rename detection)
        self.changed = list(changed)  # paths the merge changes in our tree


class Engine:
    """Everything that reads or merges upstream; sync, status and pick are written on top of it."""

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.repo.close()

    def __init__(self, repo_path, upstream=None):
        self.repo = Repo(repo_path)
        self.config = load_config(repo_path)
        self.dropped = self.config["dropped_paths"]
        self.upstream_ref = upstream or "upstream/" + self.config["upstream_branch"]
        self._translated = {}
        self._order = None

    # -- the units ------------------------------------------------------------------------------------------------

    def upstream_tip(self):
        tip = self.repo.resolve(self.upstream_ref)
        if not tip:
            raise ToolError("there is no upstream ref %s (fetch it, or use sync --fetch)" % self.upstream_ref)
        return tip

    def units(self):
        """The first-parent commits after the fork point, oldest first: dicts with sha, parents and message."""
        tip = self.upstream_tip()
        fork = self.repo.resolve(self.config["fork_point"])
        if not fork:
            raise ToolError("the fork point %s is not in this repository" % self.config["fork_point"])
        if self.repo.run("merge-base", "--is-ancestor", fork, tip, check=False).returncode != 0:
            raise ToolError("the fork point is not an ancestor of %s" % self.upstream_ref)
        raw = self.repo.run("log", "--first-parent", "--reverse", "-z", "--format=%H%x1f%P%x1f%B",
                            "%s..%s" % (fork, tip)).stdout.decode("utf-8", "replace")
        units = []
        for record in raw.split("\0"):
            if not record.strip("\n"):
                continue
            sha, parents, message = record.lstrip("\n").split("\x1f", 2)
            parents = parents.split()
            title, description, url = parse_message(message, len(parents) > 1)
            units.append({"sha": sha, "parents": parents, "title": title, "description": description, "mr_url": url})
        self._order = {unit["sha"]: index for index, unit in enumerate(units)}
        return units

    def order(self):
        if self._order is None:
            self.units()
        return self._order

    def find_unit(self, text):
        """The unit named by a full or abbreviated sha."""
        found = [unit for unit in self.units() if unit["sha"].startswith(text.lower())]
        if not found:
            raise ToolError("%s is not a first-parent commit of %s after the fork point" % (text, self.upstream_ref))
        if len(found) > 1:
            raise ToolError("%s is ambiguous: it starts %d units" % (text, len(found)))
        return found[0]

    def taken_in_history(self):
        """Upstream shas named by an `Upstream-Commit:` trailer somewhere in HEAD's history."""
        text = self.repo.out("log", "--format=%B", "-E", "--grep=^Upstream-Commit: [0-9a-f]{40}", "HEAD")
        return set(re.findall(r"(?m)^Upstream-Commit: ([0-9a-f]{40})\s*$", text))

    # -- translation ----------------------------------------------------------------------------------------------

    def _translate_blob(self, path, mode, sha):
        """The id of the blob with the OpenFallout text of an upstream file (the same id when nothing changes)."""
        if mode == "120000":
            return sha
        key = (path, sha)
        if key not in self._translated:
            data = self.repo.blob(sha)
            changed = upstream_translate.translate_content(path, data)
            if changed == data:
                self._translated[key] = sha
            else:
                self._translated[key] = self._hash(changed)
        return self._translated[key]

    def _hash(self, data):
        return self.repo.run("hash-object", "-w", "--stdin", input=data).stdout.decode().strip()

    def changed_files(self, unit):
        """[(old mode, old sha, new mode, new sha, upstream path)] of what the unit changes against its first parent."""
        raw = self.repo.run("diff-tree", "-r", "--no-renames", "-z", unit["parents"][0], unit["sha"]).stdout
        parts = raw.decode("utf-8", "surrogateescape").split("\0")
        files = []
        for index in range(0, len(parts) - 1, 2):
            old_mode, new_mode, old_sha, new_sha, _status = parts[index].lstrip(":").split(" ")
            files.append((old_mode, old_sha, new_mode, new_sha, parts[index + 1]))
        return files

    def prepare(self, unit, files):
        """Build the synthetic commits B (the unit's first parent) and T (the unit) restricted to the files the fork
        carries, translated. None when the unit has no such file."""
        base, theirs, expected, modified = [], [], [], []
        for old_mode, old_sha, new_mode, new_sha, path in files:
            if is_dropped(path, self.dropped):
                continue
            target = upstream_translate.translate_path(path)
            expected.append(target)
            had = old_mode not in ("000000", "160000")
            has = new_mode not in ("000000", "160000")  # a submodule pointer cannot be merged as a file
            if had:
                base.append((old_mode, self._translate_blob(path, old_mode, old_sha), target))
            if has:
                theirs.append((new_mode, self._translate_blob(path, new_mode, new_sha), target))
            if had and has:
                modified.append(target)
        if not expected:
            return None
        base_commit = self.repo.commit_tree(self.repo.make_tree(base), message="base of " + unit["sha"])
        theirs_commit = self.repo.commit_tree(self.repo.make_tree(theirs), parent=base_commit,
                                              message="change of " + unit["sha"])
        return Prepared(base_commit, theirs_commit, expected, modified)

    # -- merging --------------------------------------------------------------------------------------------------

    def classify(self, unit, head, in_history):
        """What would happen to a unit on top of the commit `head`: (Outcome, Prepared or None)."""
        if unit["sha"] in in_history:
            return Outcome("taken", note="found by its Upstream-Commit trailer in the history"), None
        files = self.changed_files(unit)
        prepared = self.prepare(unit, files)
        if prepared is None:
            note = ("it changes no files" if not files
                    else "every file it changes is one the fork does not carry (dropped_paths)")
            return Outcome("not-applicable", note=note), None
        outcome = self.evaluate(prepared, head)
        outcome.files = len(prepared.expected)
        return outcome, prepared

    def evaluate(self, prepared, head):
        """Merge the prepared change onto a commit without touching any working tree and say what would happen."""
        result = self.repo.run("merge-tree", "--write-tree", "--name-only", "-z", "--merge-base=" + prepared.base,
                               head, prepared.theirs, check=False)
        if result.returncode not in (0, 1):
            raise ToolError("git merge-tree failed: " + result.stderr.decode("utf-8", "replace").strip())
        fields = result.stdout.decode("utf-8", "surrogateescape").split("\0")
        tree, conflicted = fields[0], []
        for name in fields[1:]:
            if not name:
                break
            conflicted.append(name)
        if result.returncode == 1:
            return Outcome("conflict", paths=conflicted, note=self._conflict_note(prepared, head, conflicted))
        if tree == self.repo.out("rev-parse", head + "^{tree}"):
            return Outcome("already-present")
        raw = self.repo.run("diff-tree", "-r", "--no-renames", "--name-only", "-z", head, tree).stdout
        changed = [name for name in raw.decode("utf-8", "surrogateescape").split("\0") if name]
        other = sorted(set(changed) - set(prepared.expected))
        return Outcome("taken", tree=tree, other=other, changed=changed)

    def _conflict_note(self, prepared, head, conflicted):
        """Say so when the conflict is a change to a file this fork deleted: that one is a decision for a person."""
        wanted = [path for path in conflicted if path in prepared.modified]
        if wanted:
            present = set(self.repo.out("ls-tree", "--name-only", "-r", "-z", head, "--", *wanted)
                          .split("\0"))
            gone = [path for path in wanted if path not in present]
            if gone:
                return DELETED_NOTE
        return None

    # -- the commit of a taken unit --------------------------------------------------------------------------------

    def authors(self, unit):
        """(author, date, co-authors): the person who wrote most of a merge request, not the one who pressed merge.

        The author is the one with the most non-merge commits between the first parent and the other parents
        (ties go to the one who wrote last); the date is that author's last commit; the others become co-authors.
        A unit without such commits, and a plain commit, uses its own author.
        """
        own = self.repo.out("log", "-1", "--date=raw", "--format=%an%x1f%ae%x1f%ad", unit["sha"]).split("\x1f")
        commits = []
        if len(unit["parents"]) > 1:
            raw = self.repo.out("log", "--no-merges", "--date=raw", "--format=%an%x1f%ae%x1f%ad",
                                "^" + unit["parents"][0], *unit["parents"][1:])
            commits = [line.split("\x1f") for line in raw.splitlines() if line]
        if not commits:
            return {"name": own[0], "email": own[1], "date": own[2]}, []
        counts = collections.Counter()
        last = {}
        for name, email, date in commits:  # newest first
            key = email.lower()
            counts[key] += 1
            last.setdefault(key, (name, email, date))
        ranked = sorted(counts, key=lambda key: (-counts[key], -int(last[key][2].split()[0])))
        name, email, date = last[ranked[0]]
        co_authors = ["%s <%s>" % (last[key][0], last[key][1]) for key in ranked[1:]]
        return {"name": name, "email": email, "date": date}, co_authors

    def message(self, unit, co_authors):
        parts = [unit["title"]]
        if unit["description"]:
            parts.append(unit["description"])
        trailers = ["Upstream-Commit: " + unit["sha"]]
        if unit["mr_url"]:
            trailers.append("Upstream-Merge-Request: " + unit["mr_url"])
        trailers += ["Co-authored-by: " + who for who in co_authors]
        parts.append("\n".join(trailers))
        return "\n\n".join(parts) + "\n"

    def apply_tree(self, tree):
        """Bring the index and the working tree from HEAD to a merged tree (the working tree must be clean)."""
        self.repo.run("read-tree", "-m", "-u", "HEAD", tree)

    def format_changes(self, binary):
        """Reformat the lines the staged change touches, as CI/check_format_changes.py would ask for."""
        version_file = Path(self.repo.path) / "CI" / "clang-format-version.txt"
        out = subprocess.run([binary, "--version"], capture_output=True, text=True)
        if out.returncode != 0:
            raise ToolError("cannot run clang-format at %s" % binary)
        found = out.stdout.split()[out.stdout.split().index("version") + 1] if "version" in out.stdout else ""
        if version_file.exists() and found != version_file.read_text().strip():
            raise ToolError("clang-format %s at %s; CI/clang-format-version.txt pins %s"
                            % (found, binary, version_file.read_text().strip()))
        beside = Path(binary).resolve().parent / "git-clang-format"
        script = str(beside) if beside.exists() else shutil.which("git-clang-format")
        if not script:
            raise ToolError("git-clang-format was not found next to %s or on PATH" % binary)
        env = {k: v for k, v in os.environ.items() if k != "GIT_LITERAL_PATHSPECS"}
        # The pip wheel's script starts with the interpreter of its own environment, which has the clang_format module.
        launcher = [script] if os.access(script, os.X_OK) else [sys.executable, script]
        result = subprocess.run([*launcher, "--binary", binary, "--extensions", FORMAT_EXTENSIONS, "HEAD", "--",
                                 *FORMAT_FOLDERS], cwd=self.repo.path, capture_output=True, text=True, env=env)
        if result.returncode != 0:
            raise ToolError("git clang-format failed: " + (result.stdout + result.stderr).strip())

    def commit_taken(self, unit, entry, paths, clang_format=None):
        """Commit what is staged in the working tree as the unit, with its author and its ledger line."""
        if clang_format:
            self.format_changes(clang_format)
        entries = read_ledger(self.repo.path)
        entries[unit["sha"]] = entry
        write_ledger(self.repo.path, entries, self.order())
        self.repo.run("add", "-A", "--", LEDGER_FILE, *paths)
        author, co_authors = self.authors(unit)
        env = {"GIT_AUTHOR_NAME": author["name"], "GIT_AUTHOR_EMAIL": author["email"],
               "GIT_AUTHOR_DATE": author["date"]}
        self.repo.run("commit", "--no-verify", "--quiet", "-F", "-", input=self.message(unit, co_authors).encode(),
                      env=env)
        return self.repo.out("rev-parse", "HEAD")

    def undo(self):
        """Put the working tree and the index back to HEAD (the tree was clean before any change of ours)."""
        self.repo.run("reset", "--hard", "--quiet", "HEAD")




def detail(outcome_or_entry):
    """The part of a status or progress line after the title: conflicted files, other files, the note."""
    paths = outcome_or_entry.get("paths", []) if isinstance(outcome_or_entry, dict) else outcome_or_entry.paths
    note = outcome_or_entry.get("note") if isinstance(outcome_or_entry, dict) else outcome_or_entry.note
    bits = []
    if paths:
        bits.append(", ".join(paths[:3]) + (" and %d more" % (len(paths) - 3) if len(paths) > 3 else ""))
    if note:
        bits.append(note)
    return "  [%s]" % "; ".join(bits) if bits else ""


def tail(text, lines=20):
    return "\n".join(text.rstrip("\n").split("\n")[-lines:])


# ---------------------------------------------------------------------------------------------------------------------
# status


def command_status(args):
    with Engine(args.repo, args.upstream) as engine:
        ledger = read_ledger(args.repo)
        units = [unit for unit in engine.units() if unit["sha"] not in ledger]
        in_history = engine.taken_in_history()
        head = engine.repo.resolve("HEAD")
        rows = []
        for unit in units:
            outcome, _ = engine.classify(unit, head, in_history)
            if outcome.decision == "taken" and outcome.tree:
                # The next unit is judged on top of this one, as sync would do after committing it.
                head = engine.repo.commit_tree(outcome.tree, parent=head, message="simulated " + unit["sha"])
            rows.append((unit, outcome))
        totals = collections.Counter(outcome.decision for _, outcome in rows)
        tip = engine.upstream_tip()
        if args.json:
            print(json.dumps({
                "upstream_tip": tip, "totals": dict(totals),
                "units": [{"upstream": unit["sha"], "title": unit["title"], "decision": outcome.decision,
                           "files": outcome.files, "paths": outcome.paths, "landed_in_another_file": outcome.other,
                           "note": outcome.note} for unit, outcome in rows]}, indent=2, ensure_ascii=False))
            return 0
        for unit, outcome in rows:
            line = "%-16s %s %3d files  %s%s" % (outcome.decision, unit["sha"][:10], outcome.files, unit["title"],
                                                 detail(outcome))
            if outcome.other:
                line += "  [landed in another file: %s]" % ", ".join(outcome.other)
            print(line)
        counts = ", ".join("%s %d" % (name, totals[name]) for name in DECISIONS if totals[name])
        print("%d units after the fork point are not in the ledger yet (upstream tip %s)%s"
              % (len(rows), tip[:10], ": " + counts if counts else ""))
        # The ledger never looks at a recorded unit again, so the ones that still wait for a person are named here.
        waiting = [entry for entry in ledger.values() if entry["decision"] in ("conflict", "check-failed")]
        if waiting:
            print("%d units in the ledger wait for a hand port (`pick SHA`) or a `skip`:" % len(waiting))
            for entry in waiting:
                print("%-16s %s  %s" % (entry["decision"], entry["upstream"][:10], entry.get("title", "")))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# sync


class Sync:
    """One `sync` run: decides the units in order, commits the taken ones, records the rest, optionally checks."""

    def __init__(self, engine, clang_format=None):
        self.engine = engine
        self.repo = engine.repo
        self.clang_format = clang_format
        self.results = collections.OrderedDict()  # unit sha -> {"unit", "entry", "commit", "other"}
        self.run_units = []
        self.record_commit = None
        self.start_head = self.repo.resolve("HEAD")
        self.in_history = engine.taken_in_history()

    def decide(self, unit):
        outcome, _ = self.engine.classify(unit, self.repo.resolve("HEAD"), self.in_history)
        result = {"unit": unit, "commit": None, "other": outcome.other}
        if outcome.decision == "taken" and outcome.tree:
            note = ("landed in another file: git's rename detection applied the change to a file the unit does not "
                    "name; check it" if outcome.other else None)
            entry = make_entry(unit["sha"], unit["title"], "taken", outcome.other, note)
            try:
                self.engine.apply_tree(outcome.tree)
                result["commit"] = self.engine.commit_taken(unit, entry, outcome.changed, self.clang_format)
            except ToolError:
                self.engine.undo()
                raise
        else:
            entry = make_entry(unit["sha"], unit["title"], outcome.decision, outcome.paths, outcome.note)
        result["entry"] = entry
        self.results[unit["sha"]] = result
        print("%-16s %s  %s%s" % (entry["decision"], unit["sha"][:10], unit["title"], detail(entry)), flush=True)

    def process(self, units):
        for unit in units:
            self.decide(unit)

    def finish(self):
        """Write the decisions that made no commit in one last commit."""
        pending = [result for result in self.results.values() if result["commit"] is None]
        self.record_commit = None
        if not pending:
            return
        entries = read_ledger(self.repo.path)
        for result in pending:
            entries[result["unit"]["sha"]] = result["entry"]
        write_ledger(self.repo.path, entries, self.engine.order())
        self.repo.run("add", "--", LEDGER_FILE)
        last = self.run_units[-1]["sha"][:10]
        self.repo.run("commit", "--no-verify", "--quiet", "-m",
                      "Upstream sync: record the merge requests looked at up to " + last)
        self.record_commit = self.repo.out("rev-parse", "HEAD")

    # -- the check command ----------------------------------------------------------------------------------------

    def run_check(self, command, state):
        """Run the check on a commit (None: the branch as it is) and return (passed, output)."""
        if state:
            self.repo.run("checkout", "--quiet", "--force", "--detach", state)
        result = subprocess.run(command, shell=True, cwd=self.repo.path, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        return result.returncode == 0, result.stdout.decode("utf-8", "replace")

    def check_loop(self, command):
        branch = self.repo.branch()
        verified_start = False
        while True:
            passed, final_output = self.run_check(command, None)
            if passed:
                return
            commits = [result["commit"] for result in self.results.values() if result["commit"]]
            states = [self.start_head] + commits
            outputs = {len(commits): final_output}
            try:
                if not verified_start and not commits:
                    raise ToolError("the check command fails before the first unit of the run; nothing was changed")
                if not verified_start:
                    passed, _ = self.run_check(command, self.start_head)
                    if not passed:
                        raise ToolError("the check command already fails before the first unit of the run; "
                                        "nothing was changed")
                    verified_start = True
                low, high = 0, len(commits)  # states[low] passes, states[high] fails
                while high - low > 1:
                    middle = (low + high) // 2
                    passed, outputs[middle] = self.run_check(command, states[middle])
                    low, high = (middle, high) if passed else (low, middle)
            except ToolError:
                self.repo.run("checkout", "--quiet", "--force", branch)
                self.repo.run("reset", "--hard", "--quiet", self.start_head)
                raise
            self.repo.run("checkout", "--quiet", "--force", branch)
            culprit = next(result for result in self.results.values() if result["commit"] == states[high])
            self.repo.run("reset", "--hard", "--quiet", states[high - 1])
            self.take_out(culprit["unit"], tail(outputs[high]))

    def take_out(self, unit, output):
        """Forget the commit of a unit and everything after it, record the unit as check-failed, apply the rest."""
        index = [u["sha"] for u in self.run_units].index(unit["sha"])
        for later in self.run_units[index:]:
            del self.results[later["sha"]]
        entry = make_entry(unit["sha"], unit["title"], "check-failed", note=output)
        self.results[unit["sha"]] = {"unit": unit, "commit": None, "other": [], "entry": entry}
        print("%-16s %s  %s" % ("check-failed", unit["sha"][:10], unit["title"]), flush=True)
        self.process(self.run_units[index + 1:])
        self.finish()

    # -- reports --------------------------------------------------------------------------------------------------

    def counts(self):
        counter = collections.Counter(result["entry"]["decision"] for result in self.results.values())
        return counter

    def commit_count(self):
        return sum(1 for result in self.results.values() if result["commit"]) + (1 if self.record_commit else 0)

    def summary(self, tip):
        counts = self.counts()
        return {"upstream_tip": tip, "units": len(self.results), "taken": counts["taken"],
                "conflict": counts["conflict"], "not_applicable": counts["not-applicable"],
                "already_present": counts["already-present"], "check_failed": counts["check-failed"],
                "commits": self.commit_count()}

    def report(self, tip):
        base = self.engine.config["upstream_url"]
        commit_url = (base[:-4] if base.endswith(".git") else base) + "/commit/" if base.startswith(
            "https://github.com/") else DEFAULT_COMMIT_URL
        groups = collections.defaultdict(list)
        for result in self.results.values():
            groups[result["entry"]["decision"]].append(result)

        def link(result):
            title = result["entry"]["title"].replace("[", "\\[").replace("]", "\\]")
            sha = result["unit"]["sha"]
            return "[%s](%s%s) (`%s`)" % (title, commit_url, sha, sha[:10])

        def code_list(paths, limit=12):
            shown = ", ".join("`%s`" % path for path in paths[:limit])
            return shown + (" and %d more" % (len(paths) - limit) if len(paths) > limit else "")

        counts = self.counts()
        lines = ["# Upstream sync", "",
                 "Looked at %d upstream merge requests up to `%s`." % (len(self.results), tip[:10]), "",
                 "| Decision | Units |", "| --- | ---: |"]
        for name in DECISIONS:
            if counts[name]:
                lines.append("| %s | %d |" % (name, counts[name]))
        lines.append("")

        def section(title, decision, render):
            if groups[decision]:
                lines.extend(["## %s (%d)" % (title, len(groups[decision])), ""])
                for result in groups[decision]:
                    lines.extend(render(result))
                lines.append("")

        section("Taken", "taken", lambda r: ["- " + link(r) + (" (found in the history)" if not r["commit"] else "")])
        landed = [r for r in self.results.values() if r["other"]]
        if landed:
            lines.extend(["## Landed in another file (%d)" % len(landed), "",
                          "Git's rename detection applied these changes to a file the upstream change does not "
                          "name. Check that they belong there.", ""])
            for result in landed:
                lines.append("- %s: %s" % (link(result), code_list(result["other"])))
            lines.append("")
        section("Needs a hand port", "conflict", lambda r: [
            "- " + link(r), "  - conflicted: " + code_list(r["entry"].get("paths", [])) if r["entry"].get("paths")
            else "  - conflicted: (no file listed)",
            *(["  - " + sanitize(r["entry"]["note"])] if r["entry"].get("note") else []),
            "  - `python3 CI/upstream_sync.py pick %s`" % r["unit"]["sha"]])
        section("Check failed", "check-failed", lambda r: [
            "- " + link(r), "", "  ```", *("  " + line for line in sanitize(r["entry"]["note"]).split("\n")), "  ```"])
        section("Not applicable", "not-applicable", lambda r: ["- " + link(r)])
        section("Already present", "already-present", lambda r: ["- " + link(r)])
        return "\n".join(lines).rstrip("\n") + "\n"


def ensure_upstream(engine):
    """Add the remote `upstream` when it is missing and fetch the upstream branch (the only network use)."""
    repo, config = engine.repo, engine.config
    if repo.run("remote", "get-url", "upstream", check=False).returncode != 0:
        repo.run("remote", "add", "upstream", config["upstream_url"])
    repo.run("fetch", "--no-tags", "upstream", config["upstream_branch"])


def require_ready(engine):
    """sync, pick and skip commit on a branch and start from a clean working tree."""
    if not engine.repo.branch():
        raise ToolError("HEAD is detached; check out a branch first")
    if not engine.repo.is_clean():
        raise ToolError("the working tree has uncommitted changes; commit or stash them first")
    if Path(engine.repo.git_path(PICK_STATE_FILE)).exists():
        raise ToolError("a pick is in progress; finish it with `continue` or undo it with `abort`")


def command_sync(args):
    with Engine(args.repo, args.upstream) as engine:
        require_ready(engine)
        if args.fetch:
            ensure_upstream(engine)
        tip = engine.upstream_tip()
        ledger = read_ledger(args.repo)
        todo = [unit for unit in engine.units() if unit["sha"] not in ledger]
        if args.limit is not None:
            todo = todo[:max(args.limit, 0)]
        sync = Sync(engine, args.clang_format)
        sync.run_units = todo
        sync.process(todo)
        if todo:
            sync.finish()
            if args.check_cmd:
                sync.check_loop(args.check_cmd)
        summary = sync.summary(tip)
        if args.summary:
            Path(args.summary).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        if args.report:
            Path(args.report).write_text(sync.report(tip), encoding="utf-8")
        counts = sync.counts()
        print("%d units looked at, %d commits: %s" % (
            summary["units"], summary["commits"], ", ".join("%s %d" % (n, counts[n]) for n in DECISIONS if counts[n])
            or "nothing to do"))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# pick, continue, abort, skip


def pick_state_path(engine):
    return Path(engine.repo.git_path(PICK_STATE_FILE))


def require_not_taken(engine, unit, allowed):
    entry = read_ledger(engine.repo.path).get(unit["sha"])
    if entry and entry["decision"] not in allowed:
        raise ToolError("%s is recorded as %s already" % (unit["sha"][:10], entry["decision"]))
    if unit["sha"] in engine.taken_in_history():
        raise ToolError("%s is in the history already (its Upstream-Commit trailer)" % unit["sha"][:10])


def command_pick(args):
    with Engine(args.repo, args.upstream) as engine:
        require_ready(engine)
        unit = engine.find_unit(args.sha)
        require_not_taken(engine, unit, ("conflict", "check-failed", "skipped"))
        prepared = engine.prepare(unit, engine.changed_files(unit))
        if prepared is None:
            raise ToolError("every file of %s is one the fork does not carry (dropped_paths); nothing to merge"
                            % unit["sha"][:10])
        result = engine.repo.run("cherry-pick", "--no-commit", prepared.theirs, check=False)
        conflicted = engine.repo.out("diff", "--name-only", "--diff-filter=U").splitlines()
        if result.returncode not in (0, 1) or (result.returncode == 1 and not conflicted):
            engine.undo()
            raise ToolError("git cherry-pick failed: " + (result.stderr or result.stdout).decode("utf-8", "replace"))
        state = {"upstream": unit["sha"], "head": engine.repo.resolve("HEAD")}
        pick_state_path(engine).write_text(json.dumps(state), encoding="utf-8")
        print("%s %s" % (unit["sha"][:10], unit["title"]))
        if conflicted:
            print("conflicts in:\n  " + "\n  ".join(conflicted))
        else:
            print("the change applies without conflicts and is staged")
        print("resolve and `git add` the files, then run `python3 CI/upstream_sync.py continue` "
              "(or `abort` to undo)")
    return 0


def load_pick_state(engine):
    path = pick_state_path(engine)
    if not path.exists():
        raise ToolError("no pick is in progress")
    return path, json.loads(path.read_text(encoding="utf-8"))


def command_continue(args):
    with Engine(args.repo, args.upstream) as engine:
        path, state = load_pick_state(engine)
        repo = engine.repo
        if repo.resolve("HEAD") != state["head"]:
            raise ToolError("HEAD moved since the pick started; run `abort`")
        if repo.out("diff", "--name-only", "--diff-filter=U"):
            raise ToolError("there are still conflicted files; resolve them and `git add` them")
        if repo.out("diff", "--name-only"):
            raise ToolError("there are changes that are not staged (%s); `git add` them or restore them"
                            % repo.out("diff", "--name-only").replace("\n", ", "))
        if re.search(r"(?m)^\+(<{7}|>{7})( |$)", repo.out("diff", "--cached", "-U0")):
            raise ToolError("a staged file still has conflict markers")
        paths = [p for p in repo.out("diff", "--cached", "--name-only", "-z").split("\0") if p]
        unit = engine.find_unit(state["upstream"])
        entry = make_entry(unit["sha"], unit["title"], "taken", note="ported by hand")
        commit = engine.commit_taken(unit, entry, paths, args.clang_format)
        path.unlink()
        print("committed %s: %s" % (commit[:10], unit["title"]))
    return 0


def command_abort(args):
    with Engine(args.repo, args.upstream) as engine:
        path, _ = load_pick_state(engine)
        engine.undo()
        path.unlink()
        print("the pick was undone")
    return 0


def command_skip(args):
    with Engine(args.repo, args.upstream) as engine:
        require_ready(engine)
        unit = engine.find_unit(args.sha)
        require_not_taken(engine, unit, ("conflict", "check-failed", "skipped", "not-applicable", "already-present"))
        entries = read_ledger(args.repo)
        entries[unit["sha"]] = make_entry(unit["sha"], unit["title"], "skipped", note=sanitize(args.reason).strip())
        write_ledger(args.repo, entries, engine.order())
        engine.repo.run("add", "--", LEDGER_FILE)
        engine.repo.run("commit", "--no-verify", "--quiet", "-m", "Upstream sync: skip " + unit["title"],
                        "-m", sanitize(args.reason).strip())
        print("skipped %s: %s" % (unit["sha"][:10], unit["title"]))
    return 0


# ---------------------------------------------------------------------------------------------------------------------


def default_repo():
    result = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    if result.returncode != 0:
        raise ToolError("not inside a git repository (use --repo DIR)")
    return result.stdout.strip()


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.strip().split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)

    def command(name, function, help_text, upstream=True):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("--repo", help="the repository (default: the one the current folder is in)")
        if upstream:
            p.add_argument("--upstream", help="the upstream ref (default: upstream/<upstream_branch>)")
        p.set_defaults(function=function)
        return p

    p = command("status", command_status, "dry run: what sync would decide for every unit not in the ledger")
    p.add_argument("--json", action="store_true", help="print JSON instead of lines")
    p = command("sync", command_sync, "take the units that apply, record all of them")
    p.add_argument("--fetch", action="store_true", help="fetch the upstream branch first (adds the remote if missing)")
    p.add_argument("--limit", type=int, help="stop after this many units")
    p.add_argument("--report", help="write a Markdown report (for a pull request) to this file")
    p.add_argument("--summary", help="write the counts as JSON to this file")
    p.add_argument("--clang-format", help="clang-format binary; reformat the lines each unit changed")
    p.add_argument("--check-cmd", help="shell command run at the end (for example a build); a unit that breaks it "
                                       "is taken out again")
    p = command("pick", command_pick, "merge one unit into the working tree and stop at its conflicts")
    p.add_argument("sha")
    p = command("continue", command_continue, "commit a resolved pick")
    p.add_argument("--clang-format", help="clang-format binary; reformat the lines the pick changed")
    command("abort", command_abort, "undo a pick in progress")
    p = command("skip", command_skip, "record a unit as skipped")
    p.add_argument("sha")
    p.add_argument("--reason", required=True)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        args.repo = args.repo or default_repo()
        return args.function(args)
    except ToolError as error:
        print("upstream_sync.py: %s" % error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
