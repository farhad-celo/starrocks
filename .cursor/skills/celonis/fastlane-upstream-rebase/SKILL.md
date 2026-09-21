---
name: fastlane-upstream-rebase
description: >-
  Run celonis/celostar-starrocks's Fastlane Rebase and Test workflow, repair
  normal rebase conflicts using its immutable snapshot, and retry fastlane
  finalization when instructed. Use for new fastlane-aware upstream rebases or
  failures of celonis_fastlane_rebase.yml. Do not use to repair test, build,
  compliance, E2E, child-fastlane, or infrastructure failures.
---

# Run a fastlane-aware upstream rebase

`celonis_fastlane_rebase.yml` snapshots the current fork and upstream state, removes all snapshotted fastlane commits
while rebasing the remaining Celonis series, distributes still-pending fastlanes to their owners, runs integration
tests when the central branch is ready, and creates or readies the integration PR.

Stay in `celonis/celostar-starrocks`. Do not modify workflow YAML, security settings, or shared default and upstream
base branches. Do not launch Celostar tests separately; this workflow owns its presubmit and integration tests.

## Entry points

- **New rebase:** dispatch `celonis_fastlane_rebase.yml` with `mode=start` and no `run_id`.
- **Repair a start:** use the failed start run supplied by the user. If none is supplied, select the newest failed
  `Fastlane rebase start` run whose failed-job summary or logs identify a normal Git rebase conflict.
- **Retry finalization:** use `mode=finalize` only when the user identifies the distributed operation or its failed
  finalization, or when the workflow or parent PR explicitly instructs a retry. Always pass the original start run ID,
  never a continue or finalize run ID.

Never choose a different operation when the request provides a run, PR, or fastlane branch. Before any dispatch,
search for a queued or in-progress run with the same mode and original start run ID and reuse it. Before a new start,
also reuse an existing queued or in-progress `Fastlane rebase start` run rather than creating a second snapshot.

```bash
gh workflow run celonis_fastlane_rebase.yml \
  --repo celonis/celostar-starrocks \
  -f mode=start
```

For `continue` and `finalize`, the required input is the original start run ID:

```bash
gh workflow run celonis_fastlane_rebase.yml \
  --repo celonis/celostar-starrocks \
  -f mode=<continue-or-finalize> \
  -f run_id=<original-start-run-id>
```

Record the IDs that match before dispatching, then poll `gh run list` until a new matching ID appears. Record the API
returned run ID and URL; do not construct a URL or mistake a concurrent dispatch for the requested run.

## Treat failed-job summaries as the recovery contract

Monitor the selected run through completion. When it fails, inspect its metadata and every failed job. Read the GitHub
job summary before interpreting the logs. The workflow writes run-specific recovery commands, immutable identifiers,
and the correct next mode there.

List jobs through the Actions API so each job's `check_run_url` is available, then retrieve `.output.summary` from the
corresponding check run. For example:

```bash
gh api --paginate \
  "repos/celonis/celostar-starrocks/actions/runs/<run-id>/jobs?filter=latest" \
  --jq '.jobs[] | select(.conclusion == "failure") | [.name, .id, .check_run_url] | @tsv'

check_run_url="$(gh api \
  "repos/celonis/celostar-starrocks/actions/runs/<run-id>/jobs?filter=latest" \
  --jq '.jobs[] | select(.id == <failed-job-id>) | .check_run_url')"
gh api "${check_run_url#https://api.github.com/}" --jq '.output.summary // ""'
```

Follow actionable summary instructions before deriving a recovery procedure from workflow source or logs. Preserve
the summary's exact repository, original run ID, artifact name, branch names, SHAs, PR numbers, command order, and next
mode. Validate that they belong to the selected run; never substitute current branch tips or values from a newer run.
It is acceptable to adapt checkout commands to the isolated-checkout rules below, but not to change their targets or
semantics.

Use `gh run view <run-id> --log-failed` only when the summary is absent or incomplete, or to diagnose why a summarized
command failed. If neither summary nor logs provide a safe, actionable recovery, report the failure instead of
inventing a mutation. Checkout, authentication, push rejection, runner infrastructure, build, image, test, compliance,
E2E, distribution, and child-PR failures are not normal rebase conflicts; report them without changing the central or
recovery branch unless the summary gives a specific recovery procedure within this skill's scope.

## Repair a normal start rebase conflict

The failed start creates an immutable `fastlane-manifest-<run-id>` artifact and the remote recovery branch
`fastlane-rebase/<run-id>/recovery`. Use the commands printed in that start run's summary. Do not recreate or edit the
manifest, repeat classification against today's upstream state, or perform a conventional rebase that retains or
manually selects fastlane commits.

Create an isolated, durable repair checkout:

- **Cursor Cloud Agent or Automation:** its repository checkout is already isolated. Use that durable workspace
  directly.
- **Other environments:** do not repair in the user's current checkout. Create a dedicated worktree at an explicit
  durable path outside it. Do not use `mktemp`, `/tmp`, or another platform temporary directory.

Do not use `git checkout -B`, `git switch -C`, `git branch -f`, `git reset --hard`, or `git clean`. The summary may show
`git switch -C` as a concise clean-checkout command; preserve its target by creating or resuming the exact recovery
branch safely in the isolated checkout instead. Never overwrite an existing checkout or local branch. Resume an
existing one only after confirming its manifest run ID, recovery branch, and Git state belong to the selected start.

In the repair checkout:

1. Download the named manifest artifact from the original start run.
2. Read the manifest and require its checksum to validate through `fastlane_rebase.py`, its `run_id` to equal the
   original start run ID, and its `recovery_branch` to equal `fastlane-rebase/<run-id>/recovery`.
3. Fetch the manifest's exact recovery and base branches. Before beginning, require the remote recovery branch to point
   to the manifest's `old_head` and the base branch to point to its `new_head`.
4. Check out the recovery branch at `origin/<recovery-branch>` and run the summary's
   `python3 celonis/tools/fastlane_rebase.py rebase --manifest <downloaded-manifest>` command.

The helper's interactive rebase deliberately drops every snapshotted fastlane commit and rebases only the clean
Celonis series onto the manifest's `new_head`. Do not re-add a dropped fastlane commit during conflict resolution.

## Resolve conflicts and keep the rebase recoverable

Run the rebase in the foreground of the active agent turn. If the terminal returns a session handle, keep polling that
same session. Do not detach it into `tmux`, `screen`, `nohup`, `&`, or another background process. Continue until it
completes, reaches a resolvable conflict, or reaches a genuine blocker requiring user input.

For every conflict stop:

1. Inspect `git status`, `git rebase --show-current-patch`, `git diff --cc`, and stages 1/2/3 of every unmerged file.
2. Read relevant history and nearby tests. Preserve the non-fastlane Celonis behavior while adapting it to compatible
   upstream APIs and structure.
3. Never resolve an entire conflicted file with `--ours` or `--theirs`, and never skip a commit merely to finish.
4. Remove all conflict markers, stage only resolved files, run focused checks where practical, and continue with:

```bash
git -c user.name=celostar-starrocks \
    -c user.email=celostar-starrocks@noreply.github.com \
    rebase --continue
```

If correct behavior requires a product decision that cannot be inferred from code, history, or tests, stop with the
durable checkout intact and report the file, competing behaviors, and required decision.

## Validate, push, and continue

After a manually continued rebase completes, run the helper's validation explicitly because the original helper
process stopped at the first conflict:

```bash
python3 celonis/tools/fastlane_rebase.py validate-rebase \
  --manifest <downloaded-manifest> \
  --candidate HEAD \
  --report <durable-range-diff-report>
```

Also require no active rebase, no unmerged paths, a clean worktree, and a clean `git diff --check` against the
manifest's base SHA. Review and summarize the range-diff report. Unexpected missing or changed non-fastlane patches
are blockers; fastlane commits intentionally dropped by the helper are expected.

Re-fetch the recovery branch immediately before pushing. Push only `HEAD` to the exact manifest recovery branch using
the summary-required `--force-with-lease`; never force-push any other branch. If its remote SHA changed from the value
this repair checked out, stop rather than overwriting it.

Then dispatch `mode=continue` with the original start run ID and monitor that run through completion. Its `prepare` job
downloads the original manifest, validates the pushed recovery branch, and publishes the immutable central branch. If
the continue run fails, begin again by reading all failed-job summaries from that run.

## Successful workflow outcomes

- **No pending fastlanes:** the same workflow automatically runs `celonis_presubmit_test.yml` and
  `celonis_integration_test_azure.yml`. After both succeed it creates or reuses the integration PR. Do not launch or
  comment on separate Celostar test runs.
- **Pending fastlanes:** `distribute` creates a draft parent integration PR and one draft `[FASTLANE-REBASE]` child PR
  per responsible author. Record and report them. Do not restore, resolve, ready, or merge another owner's child PR as
  part of the central rebase task.
- **Distributed completion:** child owners restore their assigned commits, mark their PR ready, and use `/merge` after
  current-base CI passes. The last child merge automatically dispatches `mode=finalize`; do not create a duplicate.
  Finalization validates the distributed state, runs the same tests, and readies the parent PR.

If an automatic finalization fails, read its failed-job summaries first. Follow exact instructions to retry
`mode=finalize`, start a fresh operation, or add specifically identified missing non-fastlane commits to the central
branch. Do not squash child commits, reconstruct fastlane markers, or guess at missing work. Direct central-branch
repairs are permitted only when the workflow or parent PR explicitly identifies them as finalization recovery and the
user's request covers that recovery.

## Report

Reply in the triggering Slack thread when the task came from Slack; otherwise reply in the current conversation.
Include:

- original start run and every continue or finalize run URL
- failed job names and the recovery instructions obtained from their summaries
- manifest-pinned old head, new upstream head, base branch, recovery branch, and central branch
- conflicted files, semantic resolutions, focused verification, and range-diff findings
- integration parent PR and distributed child PR URLs, when created
- automatic test job conclusions and any remaining blocker
