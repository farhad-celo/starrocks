---
title: "[Success] Sync Rebase Test Report on {{ date | date('YYYY-MM-DD, hh:mm z') }}"
assignees: fieldsfarmer
labels: sync_rebase_test_success
---
The run is successful. The full log is in:
[https://github.com/celonis/celostar-starrocks/actions/runs/{{ env.GITHUB_RUN_ID }}](https://github.com/celonis/celostar-starrocks/actions/runs/{{ env.GITHUB_RUN_ID }})

The new default branch is: `{{ env.new_default_branch }}`

The rebase branch is: `{{ env.test_branch }}`

To create a pull request for the new default branch, please visit (remember to change the destination branch from the 
current default branch to the new default branch: `{{ env.new_default_branch }}`):

[https://github.com/celonis/celostar-starrocks/pull/new/{{ env.test_branch }}](https://github.com/celonis/celostar-starrocks/pull/new/{{ env.test_branch }})
