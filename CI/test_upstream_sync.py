#!/usr/bin/env python3
"""Tests of CI/upstream_sync.py on small throwaway repositories.

The "upstream" is a repository whose history is made of merge commits the way GitLab writes them. The "fork" is a
repository that renamed a folder (`old/` became `new/`), reworded one word (OLD became NEW), deleted a folder and a
file, and changed one line of its own. The real translation rules are replaced by those two, so the tests do not
depend on OpenMW's names and never touch the network, the real `upstream` ref or this repository's history.
"""

import contextlib
import io
import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import upstream_sync

MAINTAINER = ("Maintainer", "maintainer@example.invalid")
ALICE = ("Alice Author", "alice@example.invalid")
BOB = ("Bob Builder", "bob@example.invalid")
DAVE = ("Dave Direct", "dave@example.invalid")


def fake_path(path):
    return "new/" + path[4:] if path.startswith("old/") else path


def fake_content(path, data):
    return data.replace(b"OLD", b"NEW")


# Git without the user's or the machine's configuration, so a global setting cannot change a result.
ISOLATION = {
    "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_SYSTEM": os.devnull, "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_COMMITTER_NAME": "Syncer", "GIT_COMMITTER_EMAIL": "syncer@example.invalid",
    "GIT_AUTHOR_NAME": "Syncer", "GIT_AUTHOR_EMAIL": "syncer@example.invalid",
}


def git(repo, *args, who=None, date=None, input=None):
    env = dict(os.environ, **ISOLATION)
    if who:
        env.update(GIT_AUTHOR_NAME=who[0], GIT_AUTHOR_EMAIL=who[1], GIT_COMMITTER_NAME=MAINTAINER[0],
                   GIT_COMMITTER_EMAIL=MAINTAINER[1])
    if date:
        env.update(GIT_AUTHOR_DATE=date, GIT_COMMITTER_DATE=date)
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, env=env, input=input)
    if result.returncode != 0:
        raise AssertionError("git %s failed: %s" % (" ".join(args), result.stderr))
    return result.stdout.strip()


def write_files(repo, files):
    for path, text in files.items():
        target = Path(repo) / path
        if text is None:
            git(repo, "rm", "-q", "--", path)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        git(repo, "add", "--", path)


def commit(repo, files, message, who, date):
    write_files(repo, files)
    git(repo, "commit", "-q", "-m", message, who=who, date=date)


def lines(prefix, count, replace=None):
    replace = replace or {}
    return "".join("%s\n" % replace.get(number, "%s %d" % (prefix, number)) for number in range(1, count + 1))


UPSTREAM_AT_FORK = {
    "old/a.txt": "OLD header\n" + lines("line", 8),
    "old/b.txt": lines("b", 5),
    "old/deleted.txt": "".join("only upstream keeps this unique sentence number %d\n" % n for n in range(1, 9)),
    "old/orig.txt": lines("orig line of the long file, number", 20),
    "docs/x.md": lines("x", 5),
    "docs/y.md": lines("y", 5),
    "gone/win.txt": lines("win", 4),
    "README.md": "upstream readme\n",
}
# What the fork looks like: translated, minus what it deleted, plus its own edits.
FORK_FILES = {
    "new/a.txt": "NEW header\n" + lines("line", 8, {3: "line 3 FORK"}),
    "new/b.txt": lines("b", 5),
    "new/moved.txt": lines("orig line of the long file, number", 20, {20: "orig line of the long file, FORK"}),
    "docs/x.md": lines("x", 5, {3: "x 3 changed"}),
    "docs/y.md": lines("y", 5),
    "README.md": "fork readme\n",
}


def merge_request(up, number, branch, commits, title, description=""):
    """Merge a branch the way GitLab does; commits are (who, date, files)."""
    git(up, "checkout", "-q", "-b", branch)
    for who, date, files in commits:
        commit(up, files, "work on " + branch, who, date)
    git(up, "checkout", "-q", "master")
    body = [title] + ([description] if description else [])
    message = "Merge branch '%s' into 'master'\n\n%s\n\nSee merge request OpenMW/openmw!%d" % (
        branch, "\n\n".join(body), number)
    git(up, "merge", "--no-ff", "-q", "-m", message, branch, who=MAINTAINER, date="2026-06-01T12:00:00+00:00")


def quiet(repo):
    """No background housekeeping in a test repository: git starts it after a commit or a fetch, and its lock file
    appeared and vanished while a test copied the repository (seen once on 22 November 2026: copytree failed on
    .git/objects/maintenance.lock)."""
    git(repo, "config", "gc.auto", "0")
    git(repo, "config", "maintenance.auto", "false")


def build_world(root):
    """Create root/up (upstream history) and root/fork (a renamed copy that fetched upstream/master)."""
    up, fork = Path(root) / "up", Path(root) / "fork"
    git(root, "init", "-q", "-b", "master", str(up))
    quiet(up)
    commit(up, UPSTREAM_AT_FORK, "Fork point", MAINTAINER, "2026-01-01T12:00:00+00:00")
    fork_point = git(up, "rev-parse", "HEAD")
    merge_request(up, 101, "first", [
        (ALICE, "2026-03-01T10:00:00+00:00", {"old/a.txt": "OLD header\n" + lines("line", 8, {6: "line 6 OLD fixed"})}),
        (ALICE, "2026-03-03T10:00:00+00:00", {"old/b.txt": lines("b", 5, {2: "b 2 changed"})}),
        (BOB, "2026-03-05T10:00:00+00:00", {"docs/y.md": lines("y", 5, {5: "y 5 by bob"})}),
    ], "Fix the first thing (#77)", "Fixes #123 and !45, thanks @carol.\n\nCloses #283")
    merge_request(up, 102, "ci-only", [
        (ALICE, "2026-03-06T10:00:00+00:00", {"gone/win.txt": lines("win", 4, {1: "win 1 changed"}),
                                             "README.md": "upstream readme 2\n"}),
    ], "Only files the fork drops")
    merge_request(up, 103, "mixed", [
        (BOB, "2026-03-07T10:00:00+00:00", {"old/b.txt": lines("b", 5, {2: "b 2 changed", 5: "b 5 BREAK"}),
                                            "gone/win.txt": lines("win", 4, {1: "win 1 changed", 2: "win 2 changed"})}),
    ], "Change a kept file and a dropped one")
    merge_request(up, 104, "conflict", [
        (ALICE, "2026-03-08T10:00:00+00:00", {"old/a.txt": "OLD header\n" + lines("line", 8, {3: "line 3 upstream",
                                                                                            6: "line 6 OLD fixed"})}),
    ], "Change the line the fork changed")
    merge_request(up, 105, "adds", [
        (ALICE, "2026-03-09T10:00:00+00:00", {"old/c.txt": "OLD new file\n"}),
    ], "Add a file")
    merge_request(up, 106, "present", [
        (BOB, "2026-03-10T10:00:00+00:00", {"docs/x.md": lines("x", 5, {3: "x 3 changed"})}),
    ], "Change that the fork already has")
    commit(up, {"docs/y.md": lines("y", 5, {2: "y 2 by dave", 5: "y 5 by bob"})}, "Direct commit without a merge",
           DAVE, "2026-03-11T10:00:00+00:00")
    merge_request(up, 107, "deleted", [
        (ALICE, "2026-03-12T10:00:00+00:00", {"old/deleted.txt": "".join(
            "only upstream keeps this unique sentence number %d%s\n" % (n, " changed" if n == 4 else "")
            for n in range(1, 9))}),
    ], "Change a file the fork deleted")
    merge_request(up, 108, "moved", [
        (ALICE, "2026-03-13T10:00:00+00:00", {"old/orig.txt": lines("orig line of the long file, number", 20,
                                                                    {10: "orig line 10 OLD edited"})}),
    ], "Change a file the fork moved")
    merge_request(up, 109, "again", [
        (BOB, "2026-03-14T10:00:00+00:00", {"old/b.txt": lines("b", 5, {2: "b 2 changed", 5: "b 5 BREAK again"})}),
    ], "Build on the unit that breaks the check")

    git(root, "init", "-q", "-b", "master", str(fork))
    quiet(fork)
    config = {"upstream_url": "../up", "upstream_branch": "master", "fork_point": fork_point,
              "dropped_paths": ["gone/", "README.md"], "dropped_paths_reasons": {"test": "gone/ and README.md"}}
    files = dict(FORK_FILES)
    files["CI/upstream_sync.json"] = json.dumps(config, indent=2) + "\n"
    commit(fork, files, "The fork", ("Fork Owner", "owner@example.invalid"), "2026-05-01T12:00:00+00:00")
    git(fork, "fetch", "-q", "../up", "master:refs/remotes/upstream/master")
    return fork_point


class WorldTest(unittest.TestCase):
    """Base: a world to copy, the translation rules replaced, and helpers to run the tool."""

    template = None
    fork_point = None

    @classmethod
    def setUpClass(cls):
        cls.template = tempfile.mkdtemp(prefix="upstream-sync-test-")
        with mock.patch.dict(os.environ, ISOLATION):
            cls.fork_point = build_world(cls.template)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.template, ignore_errors=True)

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="upstream-sync-run-")
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        shutil.copytree(self.template, self.root, dirs_exist_ok=True)
        self.use(self.root)

    def use(self, root):
        """Work in the world under `root`, with the real translation replaced by the test rule."""
        self.fork = Path(root) / "fork"
        self.up = Path(root) / "up"
        for patcher in (mock.patch.dict(os.environ, ISOLATION),
                        mock.patch.object(upstream_sync.upstream_translate, "translate_path", fake_path),
                        mock.patch.object(upstream_sync.upstream_translate, "translate_content", fake_content)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def tool(self, *args):
        """Run the command line in this process: (exit status, stdout, stderr)."""
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                status = upstream_sync.main(list(args) + ["--repo", str(self.fork)])
            except SystemExit as exit_:
                status = exit_.code
        return status, out.getvalue(), err.getvalue()

    def units(self):
        """The upstream units, oldest first."""
        return git(self.fork, "log", "--first-parent", "--reverse", "--format=%H", "%s..upstream/master"
                   % self.fork_point).split()

    def ledger(self):
        path = self.fork / "CI" / "upstream_ledger.jsonl"
        if not path.exists():
            return []
        # not splitlines(): that would also split inside a title that holds U+2028
        return [json.loads(line) for line in path.read_text(encoding="utf-8").split("\n") if line]

    def decisions(self):
        return [entry["decision"] for entry in self.ledger()]

    def head(self):
        return git(self.fork, "rev-parse", "HEAD")

    def read(self, path):
        return (self.fork / path).read_text(encoding="utf-8")


EXPECTED = ["taken", "not-applicable", "taken", "conflict", "taken", "already-present", "taken", "conflict", "taken",
            "taken"]


class AfterFullSyncTest(WorldTest):
    """One full `sync` of the world, inspected from many sides."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.run_root = tempfile.mkdtemp(prefix="upstream-sync-full-")
        shutil.copytree(cls.template, cls.run_root, dirs_exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.run_root, ignore_errors=True)
        super().tearDownClass()

    def setUp(self):
        # Every test reads the same finished run (none of them changes it); the first one makes it.
        self.root = self.run_root
        self.use(self.run_root)
        if not (self.fork / "CI" / "upstream_ledger.jsonl").exists():
            self.status, self.stdout, _ = self.tool("sync", "--report", str(Path(self.run_root) / "report.md"),
                                                    "--summary", str(Path(self.run_root) / "summary.json"))
            self.__class__.first_status, self.__class__.first_stdout = self.status, self.stdout
        self.report = (Path(self.run_root) / "report.md").read_text(encoding="utf-8")
        self.summary = json.loads((Path(self.run_root) / "summary.json").read_text(encoding="utf-8"))

    def test_every_unit_has_one_ledger_line_in_upstream_order(self):
        self.assertEqual(self.first_status, 0)
        self.assertEqual([entry["upstream"] for entry in self.ledger()], self.units())
        self.assertEqual(self.decisions(), EXPECTED)
        text = (self.fork / "CI" / "upstream_ledger.jsonl").read_text(encoding="utf-8")
        for line in text.splitlines():
            self.assertEqual(line, json.dumps(json.loads(line), sort_keys=True, ensure_ascii=False))

    def test_a_clean_unit_is_committed_by_the_running_identity_with_the_upstream_authors_named(self):
        first = self.units()[0]
        commit_id = git(self.fork, "log", "--format=%H", "--grep=Upstream-Commit: " + first, "HEAD")
        self.assertTrue(commit_id)
        fields = git(self.fork, "log", "-1", "--format=%an|%ae|%cn|%ce|%s", commit_id).split("|")
        self.assertEqual(fields[:4], ["Syncer", "syncer@example.invalid", "Syncer", "syncer@example.invalid"])
        self.assertEqual(fields[4], "Fix the first thing (upstream issue 77)")
        # the date is the time of the run, not the day upstream wrote the change (3 March 2026)
        self.assertNotEqual(git(self.fork, "log", "-1", "--format=%aI", commit_id)[:10], "2026-03-03")
        body = git(self.fork, "log", "-1", "--format=%B", commit_id)
        self.assertIn("Upstream-Commit: " + first, body)
        self.assertIn("Upstream-Merge-Request: https://gitlab.com/OpenMW/openmw/-/merge_requests/101", body)
        # Alice wrote two of the three commits, so she is first; Bob follows. Names only, no address.
        self.assertTrue(body.rstrip().endswith("Upstream-Author: Alice Author\nUpstream-Author: Bob Builder"), body)
        self.assertNotIn("Co-authored-by", body)
        self.assertNotIn("example.invalid", body)
        self.assertNotIn("Claude", body)
        self.assertNotIn("Anthropic", body)
        # the change itself, translated, and the ledger line in the same commit
        self.assertIn("line 6 NEW fixed", git(self.fork, "show", commit_id + ":new/a.txt"))
        ledger = git(self.fork, "show", commit_id + ":CI/upstream_ledger.jsonl")
        self.assertEqual([json.loads(line)["upstream"] for line in ledger.splitlines()], [first])

    def test_github_cannot_link_or_notify_from_the_message(self):
        first = self.units()[0]
        commit_id = git(self.fork, "log", "--format=%H", "--grep=Upstream-Commit: " + first, "HEAD")
        body = git(self.fork, "log", "-1", "--format=%B", commit_id)
        merge_request_45 = "https://gitlab.com/OpenMW/openmw/-/merge_requests/45"
        self.assertIn("Fixes upstream issue 123 and %s, thanks carol." % merge_request_45, body)
        self.assertIn("Closes upstream issue 283", body)
        self.assertNotIn("#", body)
        self.assertNotIn("@", body)
        self.assertNotIn("See merge request", body)

    def test_a_unit_that_touches_only_dropped_paths_is_not_applicable(self):
        entry = self.ledger()[1]
        self.assertEqual(entry["decision"], "not-applicable")
        self.assertEqual(self.read("README.md"), "fork readme\n")
        self.assertFalse((self.fork / "gone").exists())
        self.assertFalse([c for c in git(self.fork, "log", "--format=%B").split("\n")
                          if c == "Upstream-Commit: " + entry["upstream"]])

    def test_dropped_files_are_left_out_of_a_mixed_unit(self):
        self.assertIn("b 5 BREAK", self.read("new/b.txt"))
        self.assertFalse((self.fork / "gone").exists())
        commit_id = git(self.fork, "log", "--format=%H", "--grep=Upstream-Commit: " + self.units()[2], "HEAD")
        changed = git(self.fork, "diff-tree", "-r", "--no-commit-id", "--name-only", commit_id).split()
        self.assertEqual(sorted(changed), ["CI/upstream_ledger.jsonl", "new/b.txt"])

    def test_a_conflict_is_recorded_and_the_units_after_it_still_apply(self):
        conflict = self.ledger()[3]
        self.assertEqual((conflict["decision"], conflict["paths"]), ("conflict", ["new/a.txt"]))
        self.assertNotIn("upstream", self.read("new/a.txt"))
        self.assertIn("line 3 FORK", self.read("new/a.txt"))
        self.assertEqual(self.read("new/c.txt"), "NEW new file\n")  # the unit after it
        self.assertTrue(self.fork.joinpath("CI/upstream_ledger.jsonl").exists())

    def test_a_change_the_fork_already_has_is_recorded_as_already_present(self):
        self.assertEqual(self.ledger()[5]["decision"], "already-present")

    def test_a_unit_without_a_merge_commit_is_committed_by_the_running_identity_and_names_its_author(self):
        direct = self.units()[6]
        commit_id = git(self.fork, "log", "--format=%H", "--grep=Upstream-Commit: " + direct, "HEAD")
        self.assertEqual(git(self.fork, "log", "-1", "--format=%an|%s", commit_id),
                         "Syncer|Direct commit without a merge")
        body = git(self.fork, "log", "-1", "--format=%B", commit_id)
        self.assertNotIn("Upstream-Merge-Request", body)
        self.assertIn("Upstream-Author: Dave Direct", body)
        self.assertNotIn("example.invalid", body)

    def test_a_change_to_a_file_the_fork_deleted_stays_a_conflict_with_a_note(self):
        entry = self.ledger()[7]
        self.assertEqual(entry["decision"], "conflict")
        self.assertIn("upstream changed a file this fork deleted; add it to dropped_paths or port it by hand",
                      entry["note"])
        self.assertEqual(entry["paths"], ["new/deleted.txt"])
        self.assertFalse((self.fork / "new" / "deleted.txt").exists())

    def test_a_change_that_rename_detection_moves_to_another_file_is_listed(self):
        entry = self.ledger()[8]
        self.assertEqual((entry["decision"], entry["paths"]), ("taken", ["new/moved.txt"]))
        self.assertIn("landed in another file", entry["note"])
        self.assertIn("orig line 10 NEW edited", self.read("new/moved.txt"))
        self.assertIn("landed in another file", self.report.lower())
        self.assertIn("`new/moved.txt`", self.report)

    def test_the_decisions_without_a_commit_are_recorded_by_one_last_commit(self):
        self.assertEqual(git(self.fork, "log", "-1", "--format=%s"),
                         "Upstream sync: record the merge requests looked at up to " + self.units()[-1][:10])
        self.assertEqual(git(self.fork, "log", "--format=%s", "-8").count("Upstream sync: record"), 1)
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")

    def test_the_summary(self):
        self.assertEqual(self.summary, {
            "upstream_tip": self.units()[-1], "units": 10, "taken": 6, "conflict": 2, "not_applicable": 1,
            "already_present": 1, "check_failed": 0, "commits": 7})

    def test_the_report(self):
        for heading in ("## Taken (6)", "## Needs a hand port (2)", "## Not applicable (1)",
                        "## Already present (1)"):
            self.assertIn(heading, self.report)
        self.assertNotIn("## Check failed", self.report)
        self.assertIn("| taken | 6 |", self.report)
        self.assertIn("python3 CI/upstream_sync.py pick " + self.units()[3], self.report)
        self.assertIn("`new/a.txt`", self.report)
        self.assertIn("(https://github.com/OpenMW/openmw/commit/%s)" % self.units()[0], self.report)
        self.assertIn("Fix the first thing (upstream issue 77)", self.report)
        self.assertNotRegex(self.report, r"#\d")

    def test_a_second_sync_looks_at_nothing(self):
        before = self.head()
        status, out, _ = self.tool("sync")
        self.assertEqual(status, 0)
        self.assertIn("0 units looked at", out)
        self.assertEqual(self.head(), before)

    def test_status_after_a_sync_has_nothing_to_say(self):
        status, out, _ = self.tool("status")
        self.assertEqual(status, 0)
        self.assertIn("0 units after the fork point are not in the ledger yet", out)


class CommandTest(WorldTest):
    def test_status_lists_every_unit_and_changes_nothing(self):
        head = self.head()
        commits = git(self.fork, "rev-list", "--count", "--all")
        status, out, _ = self.tool("status", "--json")
        self.assertEqual(status, 0)
        report = json.loads(out)
        self.assertEqual([unit["decision"] for unit in report["units"]], EXPECTED)
        self.assertEqual([unit["upstream"] for unit in report["units"]], self.units())
        self.assertEqual(report["totals"], {"taken": 6, "not-applicable": 1, "conflict": 2, "already-present": 1})
        self.assertEqual(report["units"][8]["landed_in_another_file"], ["new/moved.txt"])
        self.assertEqual(self.head(), head)
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")
        self.assertEqual(self.ledger(), [])
        self.assertEqual(git(self.fork, "rev-list", "--count", "--all"), commits)
        status, text, _ = self.tool("status")
        self.assertEqual(len([line for line in text.splitlines() if line.startswith("taken")]), 6)
        self.assertIn("10 units after the fork point are not in the ledger yet", text)
        self.assertNotIn("new/c.txt", git(self.fork, "ls-files"))

    def test_status_simulates_in_order_so_a_clean_unit_builds_on_the_previous_one(self):
        # Unit 10 changes the line unit 3 changed: it is clean only on top of unit 3.
        _, out, _ = self.tool("status", "--json")
        self.assertEqual(json.loads(out)["units"][9]["decision"], "taken")

    def test_a_unit_whose_trailer_is_in_the_history_is_taken_even_without_a_ledger_line(self):
        unit = self.units()[4]
        git(self.fork, "commit", "-q", "--allow-empty", "-m", "Ported by hand\n\nUpstream-Commit: " + unit)
        status, out, _ = self.tool("sync")
        self.assertEqual(status, 0)
        entry = self.ledger()[4]
        self.assertEqual((entry["upstream"], entry["decision"]), (unit, "taken"))
        self.assertIn("trailer", entry["note"])
        self.assertFalse((self.fork / "new" / "c.txt").exists())  # it was not applied a second time
        commits = git(self.fork, "log", "--format=%B").count("Upstream-Commit: " + unit)
        self.assertEqual(commits, 1)

    def test_limit_stops_after_n_units(self):
        status, out, _ = self.tool("sync", "--limit", "2")
        self.assertEqual(status, 0)
        self.assertEqual(self.decisions(), ["taken", "not-applicable"])
        self.assertIn("2 units looked at", out)
        status, out, _ = self.tool("sync")
        self.assertEqual(self.decisions(), EXPECTED)

    def test_sync_needs_a_clean_tree_a_branch_and_a_ref(self):
        (self.fork / "new" / "a.txt").write_text("dirty\n")
        status, _, err = self.tool("sync")
        self.assertEqual(status, 2)
        self.assertIn("uncommitted changes", err)
        git(self.fork, "checkout", "-q", "--", ".")
        git(self.fork, "checkout", "-q", "--detach")
        self.assertEqual(self.tool("sync")[0], 2)
        git(self.fork, "checkout", "-q", "master")
        status, _, err = self.tool("status", "--upstream", "nosuch/ref")
        self.assertEqual(status, 2)
        self.assertIn("no upstream ref", err)
        self.assertEqual(self.ledger(), [])

    def test_a_bad_configuration_is_one_line(self):
        (self.fork / "CI" / "upstream_sync.json").write_text("{")
        status, _, err = self.tool("status")
        self.assertEqual(status, 2)
        self.assertEqual(len(err.strip().splitlines()), 1)

    def test_fetch_adds_the_remote_and_takes_the_new_units(self):
        self.assertEqual(self.tool("sync")[0], 0)
        merge_request(self.up, 110, "late", [(BOB, "2026-03-15T10:00:00+00:00", {"old/d.txt": "OLD late\n"})],
                      "A late change")
        status, out, err = self.tool("sync", "--fetch")
        self.assertEqual(status, 0, err)
        self.assertEqual(self.decisions()[-1], "taken")
        self.assertEqual(self.read("new/d.txt"), "NEW late\n")
        self.assertEqual(git(self.fork, "remote", "get-url", "upstream"), "../up")

    def add_late_unit(self, number, files, title, description=""):
        merge_request(self.up, number, "late%d" % number, [(BOB, "2026-03-15T10:00:00+00:00", files)], title,
                      description)

    def test_a_file_name_with_a_newline_is_taken_under_that_name(self):
        self.add_late_unit(110, {"old/two\nlines.txt": "OLD odd name\n"}, "Add a file with a newline in its name")
        status, _, err = self.tool("sync", "--fetch")
        self.assertEqual(status, 0, err)
        self.assertEqual(self.decisions()[-1], "taken")
        self.assertEqual(self.read("new/two\nlines.txt"), "NEW odd name\n")
        self.assertFalse((self.fork / "new" / "two").exists())

    def test_a_unit_that_deletes_a_file_we_carry_is_taken(self):
        # The merge has already removed the file from the index and the working tree when the commit is made; naming
        # the path to `git add` again stopped the whole sync ("pathspec did not match any files").
        self.add_late_unit(110, {"docs/y.md": None, "old/b.txt": lines("b", 5, {5: "b 5 OLD changed"})},
                           "Remove a page and change a line")
        status, _, err = self.tool("sync", "--fetch")
        self.assertEqual(status, 0, err)
        self.assertEqual(self.decisions()[-1], "taken")
        self.assertFalse((self.fork / "docs" / "y.md").exists())
        self.assertIn("b 5 NEW changed", self.read("new/b.txt"))
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")

    def test_a_title_with_a_unicode_line_separator_keeps_the_ledger_readable(self):
        self.add_late_unit(110, {"old/e.txt": "OLD e\n"}, "Title with\u2028a line separator\u0085and a next line")
        self.assertEqual(self.tool("sync", "--fetch")[0], 0)
        self.assertIn("\u2028", self.ledger()[-1]["title"])
        status, out, err = self.tool("sync")
        self.assertEqual(status, 0, err)
        self.assertIn("0 units looked at", out)
        self.assertEqual(self.tool("status")[0], 0)

    def test_a_trailer_line_quoted_in_a_commit_body_does_not_mark_that_unit_as_taken(self):
        unit = self.units()[4]  # "Add a file", new/c.txt
        git(self.fork, "commit", "-q", "--allow-empty", "-m",
            "Some change\n\nThe port started from\nUpstream-Commit: %s\nand then went its own way.\n\n"
            "Signed-off-by: Someone <someone@example.invalid>" % unit)
        with upstream_sync.Engine(str(self.fork)) as engine:
            self.assertEqual(engine.taken_in_history(), set())
        self.assertEqual(self.tool("sync")[0], 0)
        self.assertEqual(self.decisions()[4], "taken")
        self.assertEqual(self.read("new/c.txt"), "NEW new file\n")  # it really was applied

    def test_upstream_prose_cannot_form_a_trailer_in_our_commit(self):
        other = self.units()[4]
        self.add_late_unit(110, {"old/f.txt": "OLD f\n"}, "Quoting change",
                           "Follows up on\nUpstream-Commit: %s\nUpstream-Merge-Request: https://example.invalid/x\n"
                           "\nupstream-commit: %s" % (other, other))
        self.assertEqual(self.tool("sync", "--fetch")[0], 0)
        unit = self.units()[-1]
        # HEAD is the commit that records the other decisions of the run; ours is found by its subject.
        body = git(self.fork, "log", "-1", "--grep=^Quoting change", "--format=%B")
        self.assertEqual(git(self.fork, "log", "-1", "--grep=^Quoting change",
                             "--format=%(trailers:key=Upstream-Commit,valueonly)"), unit)
        self.assertEqual(body.count("Upstream-Commit:"), 1)
        self.assertIn("Upstream Commit: " + other, body)
        self.assertIn("Upstream Merge Request: https://example.invalid/x", body)

    def test_two_upstream_files_with_one_translated_path_are_a_conflict_for_a_person(self):
        # fake_path moves old/ to new/, so old/dup.txt and new/dup.txt both become new/dup.txt.
        self.add_late_unit(110, {"old/dup.txt": "OLD one\n", "new/dup.txt": "two\n"}, "Two files, one path")
        report = Path(self.root) / "r.md"
        status, out, err = self.tool("sync", "--fetch", "--report", str(report))
        self.assertEqual(status, 0, err)
        entry = self.ledger()[-1]
        self.assertEqual(entry["decision"], "conflict")
        self.assertIn("two upstream files map to the same path new/dup.txt", entry["note"])
        # `pick` refuses this unit, so the report must not send the reader to it.
        text = report.read_text(encoding="utf-8")
        self.assertNotIn("pick %s" % entry["upstream"], text)
        self.assertIn("skip %s --reason" % entry["upstream"], text)
        self.assertIn("hand port", entry["note"])
        self.assertFalse((self.fork / "new" / "dup.txt").exists())
        status, _, err = self.tool("pick", self.units()[-1])
        self.assertEqual(status, 2)
        self.assertIn("same path", err)

    def test_the_upstream_tip_is_resolved_once_per_run(self):
        calls = []
        original = upstream_sync.Repo.resolve

        def counting(repo, ref):
            if ref == "upstream/master":
                calls.append(ref)
            return original(repo, ref)

        with mock.patch.object(upstream_sync.Repo, "resolve", counting):
            self.assertEqual(self.tool("status")[0], 0)
            self.assertEqual(len(calls), 1)
            del calls[:]
            self.assertEqual(self.tool("sync")[0], 0)
            self.assertEqual(len(calls), 1)

    def test_the_formatter_runs_on_the_changed_lines_of_each_taken_unit(self):
        bin_dir = Path(self.root) / "bin"
        bin_dir.mkdir()
        log = bin_dir / "calls.log"
        (bin_dir / "clang-format").write_text("#!/bin/sh\necho 'clang-format version 14.0.6'\n")
        (bin_dir / "git-clang-format").write_text(
            "#!/usr/bin/env python3\nimport subprocess, sys\n"
            "open(%r, 'a').write(' '.join(sys.argv[1:]) + '\\n')\n"
            "for name in subprocess.run(['git', 'diff', '--cached', '--name-only'], capture_output=True, text=True)"
            ".stdout.split():\n    open(name, 'a').write('formatted\\n')\n" % str(log))
        for name in ("clang-format", "git-clang-format"):
            (bin_dir / name).chmod((bin_dir / name).stat().st_mode | stat.S_IXUSR)
        status, out, err = self.tool("sync", "--limit", "5", "--clang-format", str(bin_dir / "clang-format"))
        self.assertEqual(status, 0, err)
        self.assertTrue(self.read("new/c.txt").endswith("formatted\n"))
        self.assertIn("--extensions c,cc,cpp,cxx,h,hpp HEAD -- apps components", log.read_text())
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")

    def test_without_the_option_nothing_is_formatted(self):
        self.tool("sync", "--limit", "5")
        self.assertEqual(self.read("new/c.txt"), "NEW new file\n")


class PickTest(WorldTest):
    def setUp(self):
        super().setUp()
        self.assertEqual(self.tool("sync")[0], 0)
        self.conflict = self.units()[3]

    def test_pick_leaves_conflict_markers_and_abort_restores_a_clean_tree(self):
        head = self.head()
        status, out, _ = self.tool("pick", self.conflict[:10])
        self.assertEqual(status, 0)
        self.assertIn("new/a.txt", out)
        self.assertIn("<<<<<<<", self.read("new/a.txt"))
        self.assertIn("line 3 upstream", self.read("new/a.txt"))
        # a second pick or a sync while one is open is refused
        self.assertEqual(self.tool("pick", self.conflict)[0], 2)
        self.assertEqual(self.tool("sync")[0], 2)
        git_dir = git(self.fork, "rev-parse", "--absolute-git-dir")
        self.assertTrue(Path(git_dir, "upstream-sync-pick.json").exists())
        self.assertEqual(self.tool("abort")[0], 0)
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")
        self.assertEqual(self.head(), head)
        self.assertFalse(Path(git_dir, "upstream-sync-pick.json").exists())
        self.assertEqual(self.tool("abort")[0], 2)  # nothing to abort any more

    def leave_the_pick_behind(self):
        """Start a pick, then move on as a person might: a new branch at the same commit with an unrelated edit."""
        self.tool("pick", self.conflict)
        git(self.fork, "checkout", "-q", "-f", "-b", "elsewhere")
        (self.fork / "new" / "b.txt").write_text("unrelated work\n")
        self.state_file = Path(git(self.fork, "rev-parse", "--absolute-git-dir"), "upstream-sync-pick.json")

    def test_abort_and_continue_on_another_branch_are_refused_and_change_nothing(self):
        self.leave_the_pick_behind()
        for command in ("abort", "continue"):
            status, _, err = self.tool(command)
            self.assertEqual(status, 2)
            self.assertEqual(len(err.strip().splitlines()), 1)
            self.assertIn("elsewhere", err)
            self.assertEqual(self.read("new/b.txt"), "unrelated work\n")
            self.assertTrue(self.state_file.exists())

    def test_abort_forget_deletes_only_the_record_of_the_pick(self):
        self.leave_the_pick_behind()
        status, _, err = self.tool("abort", "--forget")
        self.assertEqual(status, 0, err)
        self.assertFalse(self.state_file.exists())
        self.assertEqual(self.read("new/b.txt"), "unrelated work\n")
        self.assertEqual(self.tool("abort", "--forget")[0], 2)  # no pick any more

    def test_continue_commits_the_resolved_unit_as_ported_by_hand(self):
        self.tool("pick", self.conflict)
        status, _, err = self.tool("continue")
        self.assertEqual(status, 2)  # still conflicted
        self.assertIn("conflicted", err)
        (self.fork / "new" / "a.txt").write_text("NEW header\n" + lines("line", 8, {3: "line 3 merged", 6: "x"}))
        git(self.fork, "add", "new/a.txt")
        (self.fork / "new" / "b.txt").write_text("unstaged\n")
        status, _, err = self.tool("continue")
        self.assertEqual(status, 2)
        self.assertIn("not staged", err)
        git(self.fork, "checkout", "-q", "--", "new/b.txt")
        status, out, err = self.tool("continue")
        self.assertEqual(status, 0, err)
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")
        self.assertEqual(git(self.fork, "log", "-1", "--format=%an|%cn|%s"),
                         "Syncer|Syncer|Change the line the fork changed")
        body = git(self.fork, "log", "-1", "--format=%B")
        self.assertIn("Upstream-Commit: " + self.conflict, body)
        self.assertTrue(body.rstrip().endswith("Upstream-Author: Alice Author"), body)
        self.assertNotIn("Co-authored-by", body)
        self.assertNotIn("example.invalid", body)
        entry = self.ledger()[3]
        self.assertEqual((entry["decision"], entry["note"]), ("taken", "ported by hand"))
        self.assertNotIn("paths", entry)
        self.assertEqual([e["upstream"] for e in self.ledger()], self.units())
        # the commit has its own ledger line
        shown = git(self.fork, "show", "HEAD:CI/upstream_ledger.jsonl")
        self.assertIn('"note": "ported by hand"', shown)
        self.assertIn("0 units looked at", self.tool("sync")[1])

    def test_continue_refuses_leftover_conflict_markers(self):
        self.tool("pick", self.conflict)
        git(self.fork, "add", "new/a.txt")  # staged with the markers still in it
        status, _, err = self.tool("continue")
        self.assertEqual(status, 2)
        self.assertIn("conflict markers", err)
        self.tool("abort")

    def test_pick_of_a_unit_that_is_taken_is_refused(self):
        status, _, err = self.tool("pick", self.units()[0])
        self.assertEqual(status, 2)
        self.assertIn("recorded as taken", err)

    def test_pick_of_an_unknown_unit_is_refused(self):
        self.assertEqual(self.tool("pick", "deadbeef")[0], 2)

    def test_skip_records_the_reason_in_a_commit(self):
        status, out, _ = self.tool("skip", self.conflict, "--reason", "Morrowind only, see #5")
        self.assertEqual(status, 0)
        self.assertEqual(git(self.fork, "log", "-1", "--format=%s"),
                         "Upstream sync: skip Change the line the fork changed")
        entry = self.ledger()[3]
        self.assertEqual((entry["decision"], entry["note"]), ("skipped", "Morrowind only, see upstream issue 5"))
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")
        self.assertEqual(self.tool("skip", self.units()[0], "--reason", "no")[0], 2)  # taken units cannot be skipped


class CheckCommandTest(WorldTest):
    COMMAND = "if grep -rq BREAK new; then echo 'marker BREAK found'; exit 1; fi"

    def test_the_unit_that_breaks_the_check_is_taken_out_and_the_rest_stay(self):
        status, out, err = self.tool("sync", "--check-cmd", self.COMMAND)
        self.assertEqual(status, 0, err)
        # unit 3 broke it; unit 10 builds on unit 3, so it now conflicts; the others are untouched
        self.assertEqual(self.decisions(), ["taken", "not-applicable", "check-failed", "conflict", "taken",
                                            "already-present", "taken", "conflict", "taken", "conflict"])
        failed = self.ledger()[2]
        self.assertIn("marker BREAK found", failed["note"])
        self.assertNotIn("BREAK", self.read("new/b.txt"))
        self.assertIn("b 2 changed", self.read("new/b.txt"))  # unit 1's change to the same file is still there
        self.assertEqual(self.read("new/c.txt"), "NEW new file\n")
        self.assertIn("orig line 10 NEW edited", self.read("new/moved.txt"))
        taken = git(self.fork, "log", "--format=%B").count("Upstream-Commit: ")
        self.assertEqual(taken, 4)
        self.assertNotIn("Upstream-Commit: " + self.units()[2], git(self.fork, "log", "--format=%B"))
        self.assertEqual(git(self.fork, "status", "--porcelain"), "")
        self.assertEqual(git(self.fork, "symbolic-ref", "--short", "HEAD"), "master")
        # nothing is looked at twice
        self.assertIn("0 units looked at", self.tool("sync")[1])

    def test_a_check_that_passes_changes_nothing(self):
        status, _, _ = self.tool("sync", "--check-cmd", "true")
        self.assertEqual(status, 0)
        self.assertEqual(self.decisions(), EXPECTED)

    def test_a_check_that_fails_before_the_first_unit_changes_nothing(self):
        head = self.head()
        status, _, err = self.tool("sync", "--check-cmd", "echo broken; exit 1")
        self.assertEqual(status, 2)
        self.assertIn("before the first unit", err)
        self.assertEqual(self.head(), head)
        self.assertEqual(self.ledger(), [])
        self.assertEqual(git(self.fork, "symbolic-ref", "--short", "HEAD"), "master")

    def test_the_check_failure_is_in_the_report_and_the_summary(self):
        report, summary = Path(self.root) / "r.md", Path(self.root) / "s.json"
        self.tool("sync", "--check-cmd", self.COMMAND, "--report", str(report), "--summary", str(summary))
        text = report.read_text(encoding="utf-8")
        self.assertIn("## Check failed (1)", text)
        self.assertIn("marker BREAK found", text)
        data = json.loads(summary.read_text(encoding="utf-8"))
        self.assertEqual((data["check_failed"], data["taken"], data["conflict"], data["commits"]), (1, 4, 3, 5))


    def test_a_check_that_fails_without_a_word_still_gets_a_report(self):
        # A build whose output goes to a log file prints nothing: the unit has no note, and the report must not need one.
        report = Path(self.root) / "r.md"
        status, _, err = self.tool("sync", "--check-cmd", "! grep -rq BREAK new", "--report", str(report))
        self.assertEqual(status, 0, err)
        text = report.read_text(encoding="utf-8")
        self.assertIn("## Check failed (1)", text)
        self.assertIn("(the check printed nothing)", text)

class SanitizeTest(unittest.TestCase):
    def test_nothing_in_upstream_text_links_or_notifies_on_github(self):
        cases = {
            "Closes #283": "Closes upstream issue 283",
            "Fixes https://github.com/OpenBethesda/OpenFallout/issues/5.": "Fixes issue 5 of OpenBethesda/OpenFallout.",
            "see https://github.com/OpenMW/openmw/pull/12#issuecomment-3": "see pull request 12 of OpenMW/openmw",
            "code at https://github.com/OpenMW/openmw/commit/abc123": "code at https://github.com/OpenMW/openmw/commit/abc123",
            "Fixes #1, #2": "Fixes upstream issue 1, upstream issue 2",
            "see !5794 now": "see https://gitlab.com/OpenMW/openmw/-/merge_requests/5794 now",
            "thanks @carol and @dave-x": "thanks carol and dave-x",
            "mail a@b.example": "mail a@b.example",
            "OpenMW/openmw#12 and GH-13": "upstream issue 12 and upstream issue 13",
            "#include <vector> a != b, hello! &#123;": "#include <vector> a != b, hello! &#123;",
            "line\nSee merge request OpenMW/openmw!5794": "line\n",
        }
        for text, expected in cases.items():
            with self.subTest(text=text):
                self.assertEqual(upstream_sync.sanitize(text), expected)

    def test_upstream_text_cannot_credit_a_person_or_carry_an_address(self):
        raw = ("Merge branch 'x' into 'master'\n\nThe title\n\nThanks <y@example.org> for the idea.\n"
               "Co-authored-by: X <x@example.org>\n  signed-off-by: Z <z@example.org>\nREVIEWED-BY: R\n"
               "Acked-by: A\nTested-by: T\nReported-by: P\nSuggested-by: S\n"
               "Upstream-Author: Fake Person\n\nSee merge request OpenMW/openmw!5\n")
        _, description, _ = upstream_sync.parse_message(raw, True)
        self.assertEqual(description, "Thanks for the idea.\nUpstream Author: Fake Person")
        for text in ("a <b@example.org> c", "a\tCo-Authored-By: Q <q@example.org>\nb"):
            self.assertNotIn("@", upstream_sync.sanitize(text))
        self.assertEqual(upstream_sync.clean_name("Eve <eve@example.org>"), "Eve")
        self.assertEqual(upstream_sync.clean_name("eve@example.org"), "")
        self.assertEqual(upstream_sync.clean_name("  <Eve>  Smith "), "Eve Smith")

    def test_the_message_of_a_taken_unit_lists_cleaned_author_names_once_after_the_other_trailers(self):
        unit = {"sha": "a" * 40, "title": "T", "description": "D", "mr_url": "https://gitlab.com/OpenMW/openmw/-/merge_requests/5"}
        message = upstream_sync.Engine.message(None, unit, ["Eve <eve@example.org>", "Bob", "Bob", "x@example.org"])
        self.assertEqual(message, "T\n\nD\n\nUpstream-Commit: %s\nUpstream-Merge-Request: %s\n"
                                  "Upstream-Author: Eve\nUpstream-Author: Bob\n" % (unit["sha"], unit["mr_url"]))

    def test_title_description_and_merge_request_of_a_gitlab_merge_commit(self):
        raw = ("Merge branch 'x' into 'master'\n\nThe title\n\nThe description\nwith two lines\n\n"
               "See merge request OpenMW/openmw!5794\n")
        self.assertEqual(upstream_sync.parse_message(raw, True), (
            "The title", "The description\nwith two lines", "https://gitlab.com/OpenMW/openmw/-/merge_requests/5794"))
        # a merge commit without a title falls back to the subject
        self.assertEqual(upstream_sync.parse_message("Merge branch 'x' into 'master'\n", True)[:2],
                         ("Merge branch 'x' into 'master'", ""))
        self.assertEqual(upstream_sync.parse_message("Plain subject\n\nBody\n", False), ("Plain subject", "Body", None))

    def test_dropped_path_patterns(self):
        patterns = ["apps/opencs/", "README.md", "files/mac/openmw-cs*"]
        self.assertTrue(upstream_sync.is_dropped("apps/opencs/main.cpp", patterns))
        self.assertTrue(upstream_sync.is_dropped("README.md", patterns))
        self.assertTrue(upstream_sync.is_dropped("files/mac/openmw-cs.icns", patterns))
        self.assertFalse(upstream_sync.is_dropped("docs/README.md", patterns))
        self.assertFalse(upstream_sync.is_dropped("apps/opencs_tests/x.cpp", patterns))


if __name__ == "__main__":
    unittest.main()
