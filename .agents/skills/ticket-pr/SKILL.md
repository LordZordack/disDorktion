---
name: ticket-pr
description: Use when completed ticket work on a typed issue branch is ready to commit, verify, push, and open a pull request, or when cleaning up its branch after merge.
---

# Finish a DisDorktion Ticket

Publish completed work for one issue in `LordZordack/DisDorktion`. Run Git,
verification, and GitHub CLI commands from the selected DisDorktion repository
root. Use `--repo LordZordack/DisDorktion` for repository-scoped `gh` commands.

## 1. Resolve the ticket branch

Run:

```powershell
git branch --show-current
git status --short --branch
```

The branch must match:

```regex
^(feat|fix|test|chore)/issue-([0-9]+)-[a-z0-9-]+$
```

Capture group 2 as the issue number. When invoked from the shared
`juce_projects` parent, resolve the immediate `DisDorktion/` child. If the
selected checkout is not on a ticket branch, inspect `git worktree list` only
to locate an already-configured checkout for the requested issue. Continue
only when exactly one match exists; otherwise stop and ask the user to select
the intended checkout or run `ticket-get`.

## 2. Read the issue and local plan

Read `.tickets/issue-{number}.md` for the title, implementation plan, and test
plan. If it is absent, retrieve read-only issue data with:

```powershell
gh issue view {number} --repo LordZordack/DisDorktion --json number,title,body,url
```

Do not create local ticket notes during PR completion. If no local test plan is
available, derive a concise checklist from the actual verification commands.

## 3. Check the completed change set

Inspect both working-tree changes and commits relative to `origin/main`:

```powershell
git status --short
git log --oneline origin/main..HEAD
git diff --stat origin/main...HEAD
```

If there are no uncommitted changes and no commits ahead of `origin/main`,
report that there is nothing to publish and stop. If only commits remain, do
not create an empty commit.

## 4. Verify before publication

Before building, review the complete change set for documentation impact. Read
`docs/okf/index.md` and inspect whether product behavior,
architecture, parameters, MIDI, analyzer behavior, real-time constraints,
build/use instructions, or future-direction claims changed. When they did:

- update every affected OKF concept;
- update indexes when concepts were added, moved, renamed, or removed;
- add a dated entry to `docs/okf/log.md`;
- confirm README and `AGENTS.md` still provide correct entry points;
- verify every non-reserved OKF Markdown file has parseable YAML frontmatter
  with a non-empty `type`, the root index declares `okf_version: "0.2"`, and
  relative links resolve.

If no documentation update is needed, record that conclusion in the pull
request preparation notes. Do not publish a behavior-changing DisDorktion pull
request without completing this review.

Choose verification from files and commands actually present in this checkout.
If `CMakePresets.json` and the JUCE project exist, configure, build, and run
the relevant presets and CTest suite. Initialize submodules only when
`.gitmodules` declares them. Run any formatter, linter, or static analyzer
already configured by the repository. For OKF changes, use the validation
command in `.agents/rules/okf-standards.md`. Record every command and result;
identify checks that are unavailable because the project has not been added.
Do not claim readiness if a required available check fails.

## 5. Review, stage, and commit

Confirm that README or project documentation changes identified by
`ticket-start` were made. Review all changed and untracked paths before
staging. `.tickets/` is local working state and must remain uncommitted.

Stage only the specific expected ticket paths, then inspect the staged diff:

```powershell
git add -- path/to/expected/file path/to/another/expected/file
git status --short
git diff --cached --stat
```

Stop before committing if the stage contains secrets, large generated files,
build artifacts, or unrelated changes. Preserve the user's unrelated working
tree changes.

If expected uncommitted changes remain, commit with the repository's existing
conventional style and an issue reference, for example:

```powershell
git commit -m "feat: add distortion stage (issue #1)"
```

Choose the type and summary from the actual change. Do not create an empty
commit or hardcode another model's identity. If the commit fails, report the
error and stop.

## 6. Push the branch

Push the current branch. If it has no upstream, set one explicitly:

```powershell
git push --set-upstream origin {branch}
```

Otherwise run `git push`. Stop and report authentication, permission, or
non-fast-forward failures.

## 7. Create the pull request

Build a concise title from the issue and actual changes. Create a pull request
targeting `main`; its body must contain Summary, Changes, and Test Plan
sections and close the issue:

```powershell
$body = @'
## Summary

Closes #{number}

## Changes

- {concise description of the completed changes}

## Test Plan

- [x] {actual verification command and result}
- [ ] {unavailable check and reason, if any}
'@

$body | gh pr create --repo LordZordack/DisDorktion --base main --head {branch} --title "{title}" --body-file -
```

Replace the brace fields with real values and include any additional checks
from the ticket's test plan. Save the pull-request URL from the command output.

## 8. Move the issue to in review

Best-effort create the label, remove `in progress`, and add `in review`:

```powershell
gh label create "in review" --repo LordZordack/DisDorktion --color a371f7 --description "Has an open pull request under review"
gh issue edit {number} --repo LordZordack/DisDorktion --remove-label "in progress" --add-label "in review"
```

An already-existing label is not an error. If label or issue mutation fails,
warn the user but do not invalidate an already-created pull request.

## 9. Report completion

Tell the user:

- Pull request created, with its URL
- Verification commands and results
- Issue #{number} moved to **in review**, or the exact tracker-update warning
- `Closes #{number}` will close the issue when the pull request merges

## 10. Clean up the branch after merge

Run this only after the pull request has merged, never immediately after opening it. Verify the exact PR first:

```powershell
gh pr view {number} --repo LordZordack/DisDorktion --json state,baseRefName,headRefName,headRefOid
```

Require `state` = `MERGED`, `baseRefName` = `main`, and `headRefName` = the issue branch. If any check fails, leave both branches intact. Then:

1. Run `git status --short --branch`. Stop if there are tracked changes or untracked files other than local `.tickets/` notes. Preserve `.tickets/`; never stage or delete it.
2. Confirm `git rev-parse {branch}` equals the PR's `headRefOid`. If it differs, preserve the branch because it may contain work that was not merged.
3. Confirm `git worktree list` shows the issue branch only in the current checkout. Switch to `main` and update it with `git pull --ff-only origin main`.
4. Delete the local issue branch. Use `git branch -d {branch}` when Git recognizes it as merged. If the PR used squash or rebase merge, `git branch -d` may not recognize the merge; only then use `git branch -D {branch}`, guarded by the verified merged PR and matching `headRefOid` above.
5. Check whether the remote branch still exists with `git ls-remote --heads origin refs/heads/{branch}`. If absent, it was already removed. If present at the PR's `headRefOid`, delete it with `git push origin --delete {branch}`. If its SHA differs, preserve it and report the warning.

Report which refs were removed. If cleanup stops on a safety check or a command fails, preserve remaining branches and report why.
