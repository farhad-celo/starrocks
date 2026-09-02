---
name: upstream-rebase
description: >-
  Run celonis/celostar-starrocks's Rebase and Build Images workflow, repair Git
  rebase conflicts from its prepare-rebase job, and launch Celostar tests. Use
  when a linked or recent prepare-rebase job failed because of conflicts, or
  when a requested new rebase develops conflicts.
  Do not use for build, image, unit-test, compliance, or E2E failures.
---

# Rebase, build images, and launch Celostar tests

`celonis_rebase_and_build_images.yml` owns upstream synchronization, celo-base creation, image builds, and the draft
PR. The agent repairs conflicts when `prepare-rebase` fails, then launches tests directly in `celonis/celostar` after
the image workflow succeeds.

Stay in `celonis/celostar-starrocks`. Do not modify workflow YAML, security settings, or shared base branches.

## Entry points

- **New rebase:** dispatch `celonis_rebase_and_build_images.yml` with empty inputs, identify the resulting run, and watch
  `prepare-rebase`. If rebase and image builds succeed, continue to **Launch Celostar tests**.
- **Fix rebase:** use the run linked by the user or Slack message. If no run is provided, select the newest run whose
  `prepare-rebase` job failed specifically on Git conflicts.

Never choose a different run when the request provides a run, PR, or branch.

Before a new-rebase dispatch, check for an existing queued or in-progress empty-input run and reuse it. Do not call the
legacy `celonis_integration_test_azure.yml` workflow.

```bash
gh workflow run celonis_rebase_and_build_images.yml \
  --repo celonis/celostar-starrocks
```

## Confirm that the failure is a conflict

Inspect the run metadata, the `prepare-rebase` job, and its logs. Continue only when the logs show an actual
rebase conflict, such as `CONFLICT`, `could not apply`, unmerged paths, or instructions to resolve conflicts and run
`git rebase --continue`.

If the failure is caused by checkout, authentication, push rejection, runner infrastructure, build, image, test,
compliance, or E2E behavior, stop and report that failure. Do not manufacture a local rebase.

Useful commands:

```bash
gh run view <run-id> --repo celonis/celostar-starrocks --json event,headBranch,headSha,status,conclusion,jobs,url
gh run view <run-id> --repo celonis/celostar-starrocks --job <prepare-rebase-job-id> --log
```

## Pin the failed run context

Derive all repair inputs from the selected run, not from today's branch tips:

- `run_id`: selected Actions run ID
- `source_branch`: exact `the current default branch is ...` value from the `prepare-rebase` log
- `source_sha`: exact `The rebase source commit is: ...` value from the `prepare-rebase` log
- `new_celo_base`: exact `The new default branch is ...` value from the `prepare-rebase` log
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

## Create an isolated, durable repair checkout

Use this branch name at the pinned `source_sha`:

```text
cursor/rebase-<8-char-base-sha>-<run-id>
```

- **Cursor Cloud Agent or Automation:** its repository checkout is already isolated. Use that durable workspace checkout
  directly and create the repair branch there. Do not create another checkout or rebase state under `/tmp` or another
  platform temporary directory.
- **Other environments:** never repair in the user's current checkout because it may be dirty or have another operation
  in progress. Create a dedicated worktree under an explicit durable path outside that checkout. Do not use `mktemp`,
  `/tmp`, or another platform temporary directory.

Do not use `git checkout -B`, `git branch -f`, `git reset --hard`, or `git clean`. Do not move or overwrite an existing
checkout, worktree, or repair branch. If the exact repair branch already exists, inspect it and resume it only when it
belongs to the same run and base.

The workflow has already synchronized `branch-3.5-cc` and created `new_celo_base` before reaching the rebase conflict.
The agent must not push or rewrite either of those shared branches.

From the durable repair checkout, rebase the pinned source onto `origin/<new_celo_base>`:

```bash
git -c user.name=celostar-starrocks \
    -c user.email=celostar-starrocks@noreply.github.com \
    rebase "origin/<new_celo_base>"
```

## Keep long rebases recoverable

Run the rebase in the foreground of the active agent turn. If the terminal tool returns a session handle, keep polling
that same session. Never detach the rebase into `tmux`, `screen`, `nohup`, `&`, or another background process: Cloud
Agent processes do not survive every turn or runtime transition.

Do not voluntarily end or hand off the turn while the rebase is still advancing with progress that exists only in the
local checkout. A progress message must not replace continued monitoring. Continue until the rebase completes, stops
on a conflict that can be resolved, or reaches a genuine blocker that requires user input. If execution is interrupted,
resume from the durable checkout and inspect its actual Git state before running any new rebase.

## Resolve conflicts

For every stop:

1. Inspect `git status`, `git rebase --show-current-patch`, `git diff --cc`, and stages 1/2/3 of each unmerged file.
2. Read relevant history and nearby tests. Preserve Celonis-specific behavior while adapting it to compatible upstream
   APIs and structure.
3. Never resolve a whole conflicted file with `--ours` or `--theirs`. Never skip a commit merely to finish the rebase.
4. Remove every conflict marker, stage only files actually resolved, run focused checks where practical, and continue
   with the same configured Git identity.
5. Repeat until the rebase completes.

If correct behavior requires a product decision that cannot be inferred from code, history, or tests, stop. Leave the
durable repair checkout intact for resumption and report the file, both behaviors, and the decision needed.

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

## Build images and create the draft PR

1. Push only the repair branch. Use a normal push; never force-push without separate explicit authorization.
2. Before dispatching, reuse an existing queued, in-progress, or successful image workflow for the same repair branch.
3. Otherwise dispatch `celonis_rebase_and_build_images.yml` with `branch_to_build` set to the repair branch and
   `pull_request_base` set to `new_celo_base`. The workflow definition should come from the repository default branch;
   `--ref` is not needed.
4. Monitor the image workflow through completion. It creates or reuses one draft PR and logs `The release image tag
   is: ...` in `summarize`. Record the exact draft PR number and URL for the build branch. Do not create a second PR or
   infer an image tag before the manifest jobs succeed.
5. If rebase, PR creation, or any image build fails, report that failure and stop. Do not launch tests against incomplete
   images.

```bash
gh workflow run celonis_rebase_and_build_images.yml \
  --repo celonis/celostar-starrocks \
  -f "branch_to_build=<repair-branch>" \
  -f "pull_request_base=<new-celo-base>"
```

## Launch Celostar tests

After the image workflow succeeds, read its exact release image tag and run ID. Launch the three test workflows directly
from `celonis/celostar`; do not use the legacy StarRocks trigger action or `launch_workflow.yaml`.

Before each dispatch, search that Celostar workflow's `workflow_dispatch` runs by `displayTitle`. The expected titles are:

- `Presubmit - <release-image-tag>`
- `celostar_rebase_<build-run-id> - <Celostar-run-id>` for Azure E2E; match the prefix before the final run ID
- `Compliance Test - <release-image-tag>`

Reuse a queued, in-progress, or successful matching run instead of creating a duplicate. A failed, cancelled, or stale
run does not prevent a new dispatch. When dispatching, first record the matching run IDs that already exist, then poll
`gh run list` until a new matching ID appears. This prevents a concurrent workflow dispatch from being mistaken for the
run just created. Record each exact database ID and URL; do not construct a run URL from an assumed ID.

```bash
gh run list \
  --repo celonis/celostar \
  --workflow <workflow-file> \
  --event workflow_dispatch \
  --limit 100 \
  --json databaseId,displayTitle,url,status,conclusion,createdAt
```

```bash
release_image_tag=<release-image-tag-from-build-workflow>
build_run_id=<rebase-and-build-workflow-run-id>

gh workflow run presubmit-test.yml \
  --repo celonis/celostar \
  --ref main \
  -f "image_tag=${release_image_tag}"

gh workflow run e2e-test-suite-azure-v2.yml \
  --repo celonis/celostar \
  --ref main \
  -f "e2e_job_name=celostar_rebase_${build_run_id}" \
  -f "starrocks_image_tag=${release_image_tag}" \
  -f "gateway_image_tag=branch" \
  -f "is_scheduled=false"

gh workflow run compliance-test.yml \
  --repo celonis/celostar \
  --ref main \
  -f "image_tag=${release_image_tag}"
```

Do not wait for one Celostar test to finish before dispatching the next. After all three exact run IDs are known, poll
their state together with `gh run view <run-id> --repo celonis/celostar --json status,conclusion,url`. Continue until all
three have `status: completed`; do not stop after merely dispatching them or after posting an in-progress update. Test
failures are not rebase conflicts and must not cause changes to the repaired StarRocks branch.

## Attach test status to the draft PR

As soon as the three exact test URLs are known, create or update one comment on the StarRocks draft PR. Include this
hidden marker exactly once so later updates can find the same comment:

```text
<!-- celostar-rebase-test-status -->
```

The comment must contain the rebase/image-build workflow URL, release image tag, and a table with linked Presubmit,
Azure E2E, and Compliance runs plus their current statuses. Update the same comment whenever a test reaches a terminal
state and once more after all three complete. The final table must show every conclusion, including failure,
cancellation, or timeout. Do not post one comment per poll and do not mark the PR ready or merge it.

Find the existing marker comment and create or update it through the issue-comments API. Use the comment ID rather than
`--edit-last`, because another actor may have commented in the meantime:

```bash
marker='<!-- celostar-rebase-test-status -->'
comment_id=$(gh api \
  "repos/celonis/celostar-starrocks/issues/${draft_pr_number}/comments?per_page=100" \
  --jq ".[] | select(.body | contains(\"${marker}\")) | .id" | tail -n 1)

if [[ -n "${comment_id}" ]]; then
  gh api --method PATCH \
    "repos/celonis/celostar-starrocks/issues/comments/${comment_id}" \
    -f "body=${status_body}"
else
  gh api --method POST \
    "repos/celonis/celostar-starrocks/issues/${draft_pr_number}/comments" \
    -f "body=${status_body}"
fi
```

Keep the returned comment URL for the final report. If updating the PR comment fails, continue monitoring the tests,
then report the comment failure together with the test results rather than losing the run state.

## Report

Reply in the triggering Slack thread when the task came from Slack; otherwise reply in the current conversation. Include:

- original failed run and rebase/image-build run URLs
- pinned `source_sha`, `base_sha`, celo base, and repair branch
- conflicted files and semantic resolutions
- focused verification and range-diff findings
- draft PR URL
- release image tag, PR test-status comment URL, and the Presubmit, Azure E2E, and Compliance workflow URLs and conclusions
- any remaining blocker
