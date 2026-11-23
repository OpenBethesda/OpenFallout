# Upstream sync

OpenFallout is a fork of OpenMW, and it has diverged. The roadmap's decision on upstream is **Diverge**
([gap audit and roadmap](gap-audit-and-roadmap.md), decision 1): the fork no longer merges OpenMW's master, so shared
code (`mwworld`, `mwrender`, `components`) can change freely. Diverging does not mean ignoring upstream. Many upstream
fixes still apply to the fork's code, and porting them by hand is slow and easy to forget. `CI/upstream_sync.py` takes
the upstream changes that still apply, one at a time, and puts them on a branch for a person to review.

## How a unit is applied

A unit is one upstream merge request: one commit on the first-parent line of upstream's `master`, counted from the fork
point `f90f239d91` (4 October 2026). Units are taken oldest first. For each unit that is not yet in the ledger, the tool:

1. Leaves out the files in parts of upstream that the fork does not carry. The patterns are `dropped_paths` in
   `CI/upstream_sync.json`.
2. Translates the old and the new version of each changed file into OpenFallout's names, with
   `CI/upstream_translate.py`: `apps/openmw` becomes `apps/openfallout`, `MWWorld` becomes `OFWorld`, `OPENMW_` becomes
   `OPENFALLOUT_`, and so on. Both sides go through the same rules.
3. Three-way merges the translated change onto the branch.

Each unit gets one of six decisions:

| Decision | Meaning |
|---|---|
| `taken` | The change applied and has one commit. The commit has upstream's author and the trailer `Upstream-Commit: <sha>`. |
| `already-present` | The fork already has this change. |
| `not-applicable` | The change does not apply to OpenFallout, for example because it only touches parts the fork does not carry. |
| `conflict` | The merge did not apply cleanly. A person ports the change by hand. |
| `check-failed` | The change applied, but the check command (`--check-cmd`) failed with it, so the tool took it out again. |
| `skipped` | A person decided to leave the unit out. The reason is recorded with `skip --reason`. |

A clean merge is not the same as a working change. See [limits](#limits).

## The ledger

`CI/upstream_ledger.jsonl` has one line per unit, with its decision. The ledger is the cursor: a unit that has a line is
not looked at again. This is why nothing upstream is silently lost. Every unit that the tool sees ends with a decision
written down, and a unit that needs a person stays a `conflict` until it is ported or skipped with a reason.

## The weekly job

`.github/workflows/upstream-sync.yml` runs every Monday at 09:00 UTC. It can also be started by hand, with an optional
`limit`: the number of units to look at, oldest first. An empty `limit` means all of them.

The job does the following:

- If a pull request from the branch `upstream-sync/auto` is still open, the job writes a notice to its summary and stops.
  People may have pushed hand ports to that branch, and a new run would overwrite them. Merge or close the open pull
  request first.
- Otherwise it makes the branch `upstream-sync/auto` from master, runs `sync --fetch`, and writes the report to the job
  summary.
- If nothing was taken and nothing conflicted, it stops without pushing. The decisions of that run are then not kept,
  so the units are looked at again next week.
- Otherwise it force-pushes `upstream-sync/auto` and opens a **draft** pull request to master. The force push is safe
  because the job has checked that no open pull request uses the branch, and the push only replaces the commit the
  branch had at that check: a push that arrived in between makes it fail.

The pull request is a draft for two reasons. A pull request opened with the workflow's own token does not start other
workflows, and in this repository a draft runs no CI. When a person marks the pull request ready for review, that event
comes from the person and starts the normal CI.

The job needs the repository setting **Settings, Actions, General, Allow GitHub Actions to create and approve pull
requests**. When it is off, `gh pr create` fails. The job then prints an error that names the setting and gives the
compare link `https://github.com/OpenBethesda/OpenFallout/compare/master...upstream-sync/auto?expand=1`, and it fails.

The ledger reaches master only when the pull request is merged. A closed pull request takes its decisions with it.
Merge a sync pull request with a merge commit, as the merge queue does, and never squash it: each taken unit is its
own commit with upstream's author and its `Upstream-Commit:` trailer, and a squash would lose both.

## Running it by hand on a Mac

Run the commands from the repository root, on a branch with a clean working tree. `sync` needs both. The tool needs
Git 2.43 or newer (it merges with `git merge-tree --write-tree --merge-base`) and Python 3.11 or newer.

```
git remote add upstream https://github.com/OpenMW/openmw.git
git fetch --no-tags upstream master
python3 CI/upstream_sync.py status
python3 CI/upstream_sync.py status --json
python3 CI/upstream_sync.py sync --fetch --check-cmd "<the build command>"
```

- `status` is a dry run. It changes nothing. It lists what `sync` would decide for each unit that is not in the ledger
  yet, and then the units in the ledger that still wait for a hand port.
- `sync --fetch` adds the remote `upstream` if it is missing and fetches it. `sync` makes one commit per taken unit and
  one last commit that records the other decisions.
- `--check-cmd` runs a shell command at the end, normally a build. A unit that breaks it is taken out again and gets
  the `check-failed` decision.
- `--report FILE` writes the Markdown report, and `--summary FILE` writes the counts as JSON.
- `--clang-format PATH` reformats the changed C++ lines with that binary. Use the pinned version in
  `CI/clang-format-version.txt`, installed with `pip install "clang-format==$(cat CI/clang-format-version.txt)"`.

The exit status is 0 even when units conflict. A non-zero status means the tool could not work.

## Porting a conflict by hand

A `conflict` unit can be ported or skipped.

To port it:

```
python3 CI/upstream_sync.py pick SHA
# edit the files and resolve the conflict markers
git add <the changed files>
python3 CI/upstream_sync.py continue
```

`pick` puts the unit into the working tree with conflict markers. `continue` commits it, once the conflicts are resolved
and staged. To give up on the pick instead, run `python3 CI/upstream_sync.py abort`. It puts the working tree back to
the commit the pick started on, so it also discards any other edit made to tracked files since the pick: save such
edits first. `abort` and `continue` refuse to work on another branch or commit than the one the pick started on;
`abort --forget` then only forgets the pick and touches no file.

To leave the unit out:

```
python3 CI/upstream_sync.py skip SHA --reason "why it is not worth porting"
```

`skip` records the unit as `skipped` with the reason, so the ledger says why it was left out.

## Adding a pattern to dropped_paths

Add the pattern to `dropped_paths` in `CI/upstream_sync.json`, in the same form as the entries already there. Units that
already have a line in the ledger are not looked at again, so a new pattern changes only the units that are not decided
yet. Check the change with `status` before the next `sync`.

## Changing a translation rule

The rules are in `CI/upstream_translate.py`: the path rules and the identifier rules. A wrong rule is wrong on both sides
of a change, so it only matters on the lines that the change itself adds. A general rule on paths is often wrong, because
many paths keep the names "openmw" or "omw" on purpose. The comment above the rules lists them. A new file inside a
renamed place gets the new name.

To measure a rule against the real rename, run the script in its measure mode:

```
python3 CI/upstream_translate.py --measure BASE RENAMED
python3 CI/upstream_translate.py --measure BASE RENAMED --list
```

It translates every file of `BASE` and compares the result with the same file in `RENAMED`. It prints how many files
exist in both, how many are byte-identical after translation, and how many differ. `--list` also prints the first line
that differs for each file, so a rule can be checked against the change that it is meant to translate.

## Limits

- A clean merge is not proof. It shows that the text applied, not that the game builds or plays correctly. The build and
  tests of the draft pull request are the check, once the pull request is ready for review.
- Morrowind-only gameplay changes are taken too, when they apply. Staying close to upstream keeps later changes
  mergeable.
- Upstream's Lua API revision and its version bumps usually conflict. Expect those units to need a hand port.
