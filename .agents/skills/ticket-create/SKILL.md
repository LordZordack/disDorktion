---
name: ticket-create
description: Use when asked to draft or create an issue for a GitHub repository from a free-form work description.
---

# Create a GitHub Issue

Turn a work description into a grounded GitHub issue. This skill creates the
tracker entry only; it does not design or implement the solution.

Run local `git` inspection from the selected repository root. Resolve the
GitHub `owner/repository` name once from `git remote get-url origin`, then pass
it to repo-scoped `gh` commands with `--repo`. This remains reliable when Codex
runs network commands under a different Windows identity and Git would reject
the checkout as having dubious ownership.

## 1. Resolve the repository

Use the current directory when it is a Git repository. From a shared workspace
parent, inspect the immediate child repositories and their `origin` remotes.
Infer the target only when the request or project context identifies it;
otherwise ask the user.

Confirm that `origin` is hosted on GitHub before continuing. Parse either the
HTTPS form (`https://github.com/OWNER/REPOSITORY.git`) or SSH form
(`git@github.com:OWNER/REPOSITORY.git`) into `$repo = 'OWNER/REPOSITORY'`.

## 2. Verify GitHub CLI authentication

Confirm `gh` is installed, then run:

```powershell
gh auth status --hostname github.com
```

If authentication is invalid, start the terminal login flow:

```powershell
gh auth login --hostname github.com --git-protocol https --web
```

Keep the process open, give the user the displayed device URL and one-time
code, wait for them to authorize it, and then rerun `gh auth status`. Networked
`gh` commands may require sandbox escalation in Codex. Never display, copy, or
store the resulting token.

## 3. Classify and ground the issue

Classify the request as a bug, feature, or chore. Ask only when genuinely
ambiguous. Inspect the README, relevant files, tests, and recent commits just
deeply enough to write an accurate issue; do not produce an implementation
design.

## 4. Choose existing labels

Run:

```powershell
gh label list --repo $repo --limit 100
```

Use labels that already exist. Prefer `bug` for defects, `enhancement` for new
capabilities, and `documentation` for documentation-only work when available.
A chore may have no label if the repository has no suitable existing label.
Do not create or rename labels unless the user explicitly approves that change.

## 5. Draft the issue

Use a concise imperative title. For bugs, include the problem, location,
reproduction, expected behavior, and acceptance criteria. For features and
chores, use:

```markdown
### Motivation
{why this is needed}

### Proposed Approach
{brief direction, not a full implementation plan}

### Acceptance Criteria
- {observable criterion}
```

Show the complete title, body, and labels. Ask the user to **approve**,
**revise**, or **abort**. Do not publish until they explicitly approve. Apply
requested revisions and ask again.

## 6. Publish from the terminal

After approval, create the issue with `gh issue create`. In PowerShell, a
literal here-string and stdin avoid fragile escaping in multiline Markdown:

```powershell
$title = 'Concise imperative title'
$labels = 'enhancement'
$body = @'
### Motivation

Approved issue body.
'@

$repo = 'OWNER/REPOSITORY'
$body | gh issue create --repo $repo --title $title --body-file - --label $labels
```

Omit `--label` when no label was selected. Capture the issue URL printed by
`gh`, then verify the result with `gh issue view --repo $repo` and its `--json`
fields for `number`, `title`, `url`, and `labels`.

## 7. Report completion

Tell the user the issue number, title, URL, and applied labels. Suggest the
next ticket workflow only if it is configured for the same GitHub repository.
