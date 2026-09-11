---
name: celostar-default-branch-rollover
description: Safely promote an approved celonis/celostar-starrocks rebase PR to the new default branch, preserve missing Celonis commits, lock the old default, migrate dependent PRs, and announce the rollover in Slack. Use for an explicitly authorized Celostar StarRocks default-branch rollover; do not use for ordinary PR merges.
---

# Celostar default-branch rollover

Promote a rebase PR in `celonis/celostar-starrocks`, preserving Celonis-only work and moving active development to the PR's base branch. GitHub, Git, and Slack mutations require explicit authorization in the current request; loading this skill is not authorization.

## Inputs and invariants

- Require the rebase PR URL or number. Resolve the repository, PR head, PR base, current default branch, and their exact SHAs from live state.
- Treat the PR base as the new default and the current default as the old default. Stop if they are the same branch.
- Record expected SHAs before every push or settings change and fail closed if any ref changes.
- Never force-update the old or new default branch. Use temporary worktrees so the user's checkout and uncommitted changes remain untouched.
- Do not post migration progress as separate channel messages. Post one root rebase announcement after the cutover and before dependent PR mutations, then reply in its thread only after the PR migration attempts are complete.

## Workflow

1. Fetch the PR and repository state. Require `reviewDecision=APPROVED`; if not approved, make no mutations and report the blocker. Confirm the PR is open and its base is a direct ancestor of its head.

2. Audit the old default for missing Celonis commits before changing the PR head. Compare patch equivalence, not just SHAs: rebasing rewrites commit IDs, and conflict-resolved equivalents may also have different patch IDs. Investigate unmatched commits using subject, affected paths, PR provenance, and upstream PR state. Cherry-pick only commits confirmed to carry Celonis-specific changes absent from the PR head. Resolve conflicts conservatively, run focused tests, push the PR head with a lease, and then re-read its approval and checks. If approval is no longer valid, stop.

3. Require all policy-required checks other than SonarQube to pass. Do not wait for `celonis-pr-sonarqube`, `fe-coverage`, or `be-sonarQube-scan`; their pending state is not a blocker for this procedure. A non-Sonar required failure is a blocker.

4. Immediately before merging, re-fetch all refs and repeat the missing-commit check if the old default advanced. Prefer GitHub's native rebase merge. GitHub rejects native rebase merges with more than 100 commits; in that case, only when the PR history is linear and the PR base is an ancestor of the exact PR head, perform the equivalent operation by normally fast-forwarding the PR base to the exact head SHA. Do not create a merge commit or squash. Verify GitHub records the PR as merged and the new branch resolves to the expected SHA.

5. Snapshot all other open PRs whose base is the old default. Read and record the repository custom property `RepoDepotManaged`; accept only `true` or `false`. Treat the default-branch change as a short transaction: install cleanup handling first, set `RepoDepotManaged=false` when needed and verify it, change the repository default branch to the merged PR's base through the GitHub API and verify it, then immediately set `RepoDepotManaged=true` and verify it. If the property is already `false`, report that pre-existing state and proceed only under the current request's explicit authorization to finish the rollover and restore it to `true`. Restoration must be attempted even if the default-branch update or verification fails. If restoration cannot be confirmed, stop all subsequent mutations and report the repository as requiring urgent administrator action. Keep the property disabled only for this settings change, never during branch locking, PR migration, or Slack notification.

6. After the default branch is verified and `RepoDepotManaged=true` is confirmed, lock the old default explicitly. Existing `~DEFAULT_BRANCH` rules move to the new default, so they are not sufficient: preserve any existing classic protection and set `lock_branch=true`; if none exists, create a minimal full read-only protection with administrator enforcement, force pushes and deletions disabled. Verify the old branch SHA did not move and its lock is enabled.

7. Before modifying any dependent PR, post one concise root message to Slack `#celostar-dev` using live branch values and eight-character SHAs in this format:

   ```text
   Hi team,

   We have successfully rebased the Celostar StarRocks default branch.

   • Previous default: `{OLD_DEFAULT}` @ `{OLD_SHA_8}`
   • New default: `{NEW_DEFAULT}` @ `{NEW_SHA_8}` — https://github.com/celonis/celostar-starrocks/tree/{NEW_DEFAULT}

   Thanks!
   ```

   Do not include the rollover PR, preserved-commit details, `RepoDepotManaged`, branch-lock details, dependent-PR migration progress, credentials, or internal command output.

8. Migrate each snapshotted open PR, excluding the rollover PR. Re-fetch its head and metadata. For a same-repository branch with push permission, rebase its unique commits onto the new default in a temporary worktree, resolve conflicts only if the intended behavior is clear, run focused validation, and push with `--force-with-lease` against the recorded head SHA. Retarget its base to the new default only after the rebase succeeds, then verify the new default is an ancestor of the updated head. If the head is already based on the new default, only retarget it. Do not rewrite fork branches without permission; do not retarget a PR whose rebase failed. Report those PRs for owner action and continue with independent PRs.

9. After all dependent PR migration attempts and their verification are complete, reply in the root Slack announcement's thread with the final counts and outcomes: migrated and retargeted PRs, PRs whose changes were already contained in the new default, and any PRs still requiring owner action. Do not post this completion update as a new root channel message.

## Final verification

Confirm from live GitHub state that the rollover PR is merged, the new default and its SHA are correct, `RepoDepotManaged=true`, the old default retains its recorded SHA and is locked, and every reported migrated PR targets the new default with the new default as an ancestor. Remove clean temporary worktrees and summarize exact results and any unresolved PRs.
