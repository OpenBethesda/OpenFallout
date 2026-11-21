# Harness delivery pipelines

OpenFallout uses the existing **OpenBethesda** project in Harness, organization
`default`, account `Vti98EjbSdmr0xdtCsVP_w`:

<https://app.harness.io/ng/account/Vti98EjbSdmr0xdtCsVP_w/all/orgs/default/projects/OpenBethesda>

The pipelines are stored inline in Harness. The `.harness/` files are versioned
copies for review and recovery; merging them does **not** automatically update
the inline definitions. Apply reviewed changes in Harness too.

## Build and test

Harness no longer builds or tests OpenFallout. Its build pipeline, `openfallout_ci`, was an Ubuntu container build
that duplicated the Linux job of GitHub Actions; the owner switched it and its webhook triggers off in Harness when the
codebase became Mac-only. The one build is the Mac jobs of GitHub Actions (the app is `macOS / Build, test and package`, see
[CI reporting](ci-reporting.md)). What is left in Harness is the security scan and the release pipeline below.

## Source and supply-chain security

`openfallout_security` runs native Semgrep SAST, Gitleaks with redacted output,
and SSCA's cdxgen CycloneDX generation. All three completed successfully on the
initial master baseline, and the 38-component SBOM and its scorecard were uploaded to Harness.
Static C++ inventory is an initial dependency view; it is not a complete inventory
of every linked platform library or shipped installer.

Scans initially report findings without a new severity gate. The initial STO
baseline contained 27 active deduplicated findings: nine high, nine medium, and
nine low. A successful scan means ingestion worked; it does not mean zero findings.
Review findings in the pipeline execution's **Security** tab before selecting a
blocking threshold.

Security has enabled GitHub webhook triggers for master pushes and
non-draft, same-repository pull requests targeting master. PR updates abort the
previous execution. Fork PRs are excluded. The registered GitHub webhook was
verified active with a successful response. Trigger definitions live under
`.harness/triggers/`.

## Desktop releases

`openfallout_release` is manual. It orchestrates the existing GitHub Actions
`Release` workflow, which builds the one disk image, for Apple Silicon (macOS 26.0
or later), and the engine release's statement. The workflow runs for
`openfallout-*` tags only.

Run it with these variables:

| Variable | Values | Default |
| --- | --- | --- |
| `mode` | `preview`, `draft`, `publish` | `preview` |
| `source_ref` | Tested commit on master; use an immutable SHA | `master` |
| `tag` | Unique `openfallout-` tag, e.g. `openfallout-0.1.0-rc1` | Required |

`preview` only reads GitHub and prints readiness, source SHA, CI link, and blockers.
It saves that SHA in an ephemeral workspace file, so later phases keep using the
same source even if master advances while packaging runs.
The hosted preview was tested successfully. It correctly blocks release creation
until this setup is merged into master. No tag or release is created by preview.

`draft` requires the source to be on master, this setup to exist at that commit,
and its latest master **Build and test** push or manual run to have passed the
one Mac job, `macOS / Build, test and package`. A docs-only run reports that job green with every build step
skipped (each Mac job decides for itself from the changed paths, so the job is no longer skipped as a whole). Such a run
is not a build, so `CI/harness_release.py` also requires the job's step "Check the packaged app" to have passed; run the
full workflow manually on that master commit if necessary. It creates a
lightweight tag, waits for release packaging through `gh run watch`, and verifies
exactly one nonempty uploaded disk image, the Apple Silicon one
(`OpenFallout-<version>-macOS-arm64.dmg`). A second image, an Intel image, a
Windows installer or a Linux package is refused.
GitHub Actions leaves the release as a draft prerelease. Harness retries the wait
step while GitHub registers the new tag's workflow run.

`publish` performs the same checks, then promotes the verified draft to a public
**prerelease**. Running either mode against an existing matching tag resumes the
process; it never moves an existing tag. A tag for another commit is rejected.
Failed packaging must be repaired and rerun in GitHub Actions before promotion.
Draft creation and publishing were unit-tested, but no real version was published
during setup. Use the same immutable `source_ref` SHA when resuming a release.

Before a release is promoted, the gate also reads the engine release's statement
(`openfallout-engine-release.json`, made by CI from the disk image) and refuses a release whose statement is missing,
disagrees with the uploaded image's digest or the tag's commit, or whose notes lack the *supported* and *played on*
lines; see [engine-release.md](engine-release.md). It downloads that one small asset through the GitHub API; the
token that lists the draft releases also reads their assets.

The release pipeline checks out an immutable commit containing
`CI/harness_release.py`. Its application source is selected separately, preventing
a runtime source ref from replacing the privileged release script. When changing
the helper, push and review it first, then update the pipeline's pinned commit.
The pin must move after the Mac-only cut is merged: the commit pinned in
`.harness/openfallout-release.yaml` holds the old helper, which waits for the
Ubuntu, Windows and Intel Mac jobs and asks for a Windows installer and an Intel
image, so it would refuse every release now.
Credentials use the existing Harness GitHub OAuth secret by reference only;
no token value is stored in Git. CI/Security do not persist checkout credentials.

## Local checks

```sh
python3 -m unittest discover -s CI -p 'test_*.py'
actionlint .github/workflows/*.yml
```

Release tests exercise read-only preview, invalid tags, failed or unmerged source
rejection, tag immutability, draft lookup, asset requirements, and promotion.
The lightweight `Changes` job of GitHub Actions, on a Linux runner, also runs the release helper tests.
