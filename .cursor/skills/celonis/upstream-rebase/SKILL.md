---
name: upstream-rebase
description: >-
  Repair Git rebase conflicts from celonis/celostar-starrocks's Rebase and Test
  on Azure workflow. Use when a linked or recent sync-and-rebase-upstream job
  failed because of conflicts, or when a requested new rebase develops conflicts.
  Do not use for build, image, unit-test, compliance, or E2E failures.
---

# Repair Azure upstream rebase conflicts

The GitHub workflow owns upstream synchronization, celo-base creation, builds, and tests. The agent takes over only
when `sync-and-rebase-upstream` has failed because `git rebase` stopped on conflicts.

Stay in `celonis/celostar-starrocks`. Do not modify workflow YAML, security settings, or shared base branches.

## Entry points

- **New rebase:** dispatch `celonis_integration_test_azure.yml` with an empty `branch_to_test`, identify the resulting
  run, and watch `sync-and-rebase-upstream`. If that job succeeds, report the run URL; no repair is needed.
- **Fix rebase:** use the run linked by the user or Slack message. If no run is provided, select the newest run whose
  `sync-and-rebase-upstream` job failed specifically on Git conflicts.

Never choose a different run when the request provides a run, PR, or branch.

## Confirm that the failure is a conflict

Inspect the run metadata, the `sync-and-rebase-upstream` job, and its logs. Continue only when the logs show an actual
rebase conflict, such as `CONFLICT`, `could not apply`, unmerged paths, or instructions to resolve conflicts and run
`git rebase --continue`.

If the failure is caused by checkout, authentication, push rejection, runner infrastructure, build, image, test,
compliance, or E2E behavior, stop and report that failure. Do not manufacture a local rebase.

Useful commands:

```bash
gh run view <run-id> --repo celonis/celostar-starrocks --json event,headBranch,headSha,status,conclusion,jobs,url
gh run view <run-id> --repo celonis/celostar-starrocks --job <sync-job-id> --log
```

## Pin the failed run context

Derive all repair inputs from the selected run, not from today's branch tips:

- `run_id`: selected Actions run ID
- `source_branch`: exact `the current default branch is ...` value from the sync-job log
- `source_sha`: exact `The rebase source commit is: ...` value from the sync-job log
- `new_celo_base`: exact `The new default branch is ...` value from the sync-job log
- `base_sha`: commit at `origin/<new_celo_base>`

The run's event `headSha` is diagnostic only. The workflow fetches and checks out a mutable branch before rebasing, so
`headSha` may differ from the source commit it actually used. Never substitute `headSha` for the logged `source_sha`.

Fetch the two named origin branches and verify that the logged `source_sha` is available locally and belongs to the
logged `source_branch`. Verify that the 8-character SHA embedded in `new_celo_base` matches `base_sha`. If either source
value is absent from the log, the base is not identified, a remote branch is missing, or a SHA does not match, stop and
report the inconsistent run instead of guessing.

Do not substitute the repository's current default-branch HEAD or the current tip of upstream `branch-3.5-cc`.
Whenever a later tool call needs these values, recompute them from the same run metadata and log; shell variables from
a command that stopped on conflict will not persist.

## Create an isolated repair worktree

Never repair in the user's current checkout. It may be dirty or may have another branch or rebase in progress.

Create a dedicated temporary worktree at the pinned `source_sha` with this branch name:

```text
cursor/rebase-<8-char-base-sha>-<run-id>
```

Use `mktemp -d` for a parent directory and place the worktree below it. Do not use `git checkout -B`, `git branch -f`,
`git reset --hard`, or `git clean`. Do not move or overwrite an existing worktree or repair branch. If the exact repair
branch already exists, inspect it and resume it only when it belongs to the same run and base.

The workflow has already synchronized `branch-3.5-cc` and created `new_celo_base` before reaching the rebase conflict.
The agent must not push or rewrite either of those shared branches.

From the isolated worktree, rebase the pinned source onto `origin/<new_celo_base>`:

```bash
git -c user.name=celostar-starrocks \
    -c user.email=celostar-starrocks@noreply.github.com \
    rebase "origin/<new_celo_base>"
```

## Resolve conflicts

For every stop:

1. Inspect `git status`, `git rebase --show-current-patch`, `git diff --cc`, and stages 1/2/3 of each unmerged file.
2. Read relevant history and nearby tests. Preserve Celonis-specific behavior while adapting it to compatible upstream
   APIs and structure.
3. Never resolve a whole conflicted file with `--ours` or `--theirs`. Never skip a commit merely to finish the rebase.
4. Remove every conflict marker, stage only files actually resolved, run focused checks where practical, and continue
   with the same temporary Git identity.
5. Repeat until the rebase completes.

If correct behavior requires a product decision that cannot be inferred from code, history, or tests, stop. Leave the
isolated worktree intact for resumption and report the file, both behaviors, and the decision needed.

## Verify before push

Recompute `source_sha`, `new_celo_base`, and `base_sha` from the selected run. Then require:

```bash
test ! -d "$(git rev-parse --git-path rebase-merge)"
test ! -d "$(git rev-parse --git-path rebase-apply)"
test -z "$(git diff --name-only --diff-filter=U)"
test -z "$(git status --porcelain)"
git merge-base --is-ancestor "origin/<new_celo_base>" HEAD
git diff --check "origin/<new_celo_base>..HEAD"
```

Compare the original and rebased Celonis patch series:

```bash
old_base=$(git merge-base "<source_sha>" "<base_sha>")
git range-diff "${old_base}..<source_sha>" "<base_sha>..HEAD"
```

Review and summarize every range-diff change. Commit-count differences or dropped commits are allowed only when the
patch is already upstream or the semantic resolution intentionally absorbed it; otherwise stop before pushing.

## Publish and validate

1. Push only the repair branch. Use a normal push; never force-push without separate explicit authorization.
2. Dispatch `celonis_integration_test_azure.yml` with `branch_to_test` set to the repair branch. The workflow definition
   should come from the repository default branch; `--ref` is not needed.
3. Create or update one draft PR with base `new_celo_base` and head equal to the repair branch. Do not create duplicates.
4. Monitor the validation run. If it fails outside rebase conflict handling, report the failing job and stop.

```bash
gh workflow run celonis_integration_test_azure.yml \
  --repo celonis/celostar-starrocks \
  -f "branch_to_test=<repair-branch>"
```

## Report

Reply in the triggering Slack thread when the task came from Slack; otherwise reply in the current conversation. Include:

- original failed run and validation run URLs
- pinned `source_sha`, `base_sha`, celo base, and repair branch
- conflicted files and semantic resolutions
- focused verification and range-diff findings
- draft PR URL
- any remaining blocker
