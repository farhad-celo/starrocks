---
title: "[Failure] Sync Rebase Test Report on {{ date | date('YYYY-MM-DD, hh:mm z') }}"
assignees: fieldsfarmer
labels: sync_rebase_test_failure
---
The run is failed. Please check full log to debug:
[https://github.com/celonis/celostar-starrocks/actions/runs/{{ env.GITHUB_RUN_ID }}](https://github.com/celonis/celostar-starrocks/actions/runs/{{ env.GITHUB_RUN_ID }})

The new allin1 image tag is: `{{ env.allin1_image_tag }}`