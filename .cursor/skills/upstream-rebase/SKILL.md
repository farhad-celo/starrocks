---
name: upstream-rebase
description: >-
   Runs Celonis Azure upstream rebase for celonis/celostar-starrocks: dispatch
   Rebase and Test on Azure (celonis_integration_test_azure.yml), replay
   sync-and-rebase-upstream, and semantically resolve git rebase conflicts.
   Use when Slack says new rebase or fix rebase, when Azure rebase CI fails on
   conflicts, or when asked to rebase onto branch-3.5-cc / a *-celo base.
---

# Upstream rebase (celostar-starrocks)

Stay in **celonis/celostar-starrocks**. Do not switch to other Celonis repos.

## Commands

| Slack / user text | Mode |
|---|---|
| `new rebase` | Dispatch the scheduled-style workflow, then repair if rebase conflicts |
| `fix rebase` | Repair the latest rebase (or the linked run / PR / branch) |

Build, image, or E2E failures are **not** rebase conflicts. Stop and report those.

## Constants

- Workflow file: `celonis_integration_test_azure.yml` (name: Rebase and Test on Azure)
- Upstream remote: `https://github.com/starrocks/starrocks.git`
- Upstream branch: `branch-3.5-cc` (`LATEST_UPSTREAM_BRANCH`)
- Source of Celonis commits: the repo **default branch** at trigger time
- New celo base: `branch-3.5-cc-<8-char-upstream-sha>-celo`
- Test branch (workflow): `gh-branch-3.5-cc-<8-char-sha>-<timestamp>`
- Repair branch (this agent): `cursor/rebase-<8-char-sha>-<run-id>`

## NEW REBASE

1. Dispatch with **empty** `branch_to_test` (full sync + rebase + tests):

   ```bash
   gh workflow run celonis_integration_test_azure.yml --repo celonis/celostar-starrocks
   ```

2. Watch the run, especially job `sync-and-rebase-upstream`.
3. If that job succeeds, report the run URL and stop. Do not invent extra rebases.
4. If it fails with unmerged files / rebase conflict, run **Repair** below, then dispatch validation with `branch_to_test` set to the repaired branch.

## FIX REBASE

1. If the message has a GitHub Actions run, PR, or branch URL/name, use **only** that context.
2. Otherwise pick the most recent `celonis_integration_test_azure.yml` attempt that failed in `sync-and-rebase-upstream` (or left an incomplete rebase).
3. Run **Repair**, then validate.

## Repair (replay `sync-and-rebase-upstream`)

Match the workflow; do not rebase onto `main` unless that is actually the default branch.

```bash
git config user.email "celostar-starrocks@noreply.github.com"
git config user.name "celostar-starrocks"

git remote add upstream https://github.com/starrocks/starrocks.git 2>/dev/null || \
  git remote set-url upstream https://github.com/starrocks/starrocks.git
git fetch --no-tags upstream branch-3.5-cc
git fetch origin

git branch -f branch-3.5-cc upstream/branch-3.5-cc
git push origin branch-3.5-cc

latest_commit=$(git rev-parse --short=8 upstream/branch-3.5-cc)
newdefaultbranch="branch-3.5-cc-${latest_commit}-celo"
default_branch=$(gh repo view celonis/celostar-starrocks --json defaultBranchRef --jq .defaultBranchRef.name)

git fetch origin "${default_branch}"
if git ls-remote --exit-code --heads origin "${newdefaultbranch}" >/dev/null; then
  git checkout -B "${newdefaultbranch}" "origin/${newdefaultbranch}"
else
  git checkout -B "${newdefaultbranch}" upstream/branch-3.5-cc
  git push --set-upstream origin "${newdefaultbranch}"
fi

git checkout -B "cursor/rebase-${latest_commit}-${RANDOM}" "origin/${default_branch}"
GIT_EDITOR=true git rebase "${newdefaultbranch}"
```

If rebase stops:

1. Inspect `git status`, `git rebase --show-current-patch`, `git diff --cc`, and stage 1/2/3 of each unmerged file.
2. Resolve **semantically**: keep Celonis-specific behavior; take compatible upstream changes.
3. Never whole-file `--ours` / `--theirs`. Never `git rebase --skip` or `--abort`.
4. Remove all conflict markers, `git add` only resolved files, then `GIT_EDITOR=true git rebase --continue`.
5. Repeat until rebase finishes.

If a conflict needs a product decision that cannot be inferred from code and history, **stop**. Do not invent behavior. Report the file, both sides, and the decision needed.

## Verify (required before push)

```bash
test ! -e .git/rebase-merge && test ! -e .git/rebase-apply
test -z "$(git diff --name-only --diff-filter=U)"
test -z "$(git status --porcelain)"
git merge-base --is-ancestor "${newdefaultbranch}" HEAD
git diff --check "${newdefaultbranch}..HEAD"
test "$(git rev-list --count "${newdefaultbranch}..HEAD")" -gt 0
```

Optional: `git range-diff` of the old Celonis range vs the new range — report differences; do not fail solely on range-diff noise.

## After a successful repair

1. `git push --set-upstream origin HEAD`
2. Dispatch validation:

   ```bash
   gh workflow run celonis_integration_test_azure.yml \
     --repo celonis/celostar-starrocks \
     --ref <repaired-branch> \
     -f "branch_to_test=<repaired-branch>"
   ```

3. Open a **draft** PR:
   - base: `${newdefaultbranch}` (the synced celo branch)
   - head: repaired branch
   - title: `Integration rebase: <head> → <newdefaultbranch>`

Do not change GitHub workflow YAML or security settings.

## Slack / run summary

Reply in the **triggering thread** with:

- Mode (`new rebase` / `fix rebase`)
- Workflow run URL(s)
- `newdefaultbranch` and repaired branch
- Draft PR URL (if created)
- Conflicted files and how they were resolved
- Anything still blocked
