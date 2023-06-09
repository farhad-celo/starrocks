---
title: "[Success] Sync Rebase Build Dev Env Image Report on {{ date | date('YYYY-MM-DD, hh:mm z') }}"
assignees: fieldsfarmer
labels: sync_rebase_build_dev_env_success
---
The run is successful. The full log is in:
[https://github.com/celonis/celostar-starrocks/actions/runs/{{ env.GITHUB_RUN_ID }}](https://github.com/celonis/celostar-starrocks/actions/runs/{{ env.GITHUB_RUN_ID }})

The new dev env image is: `ghcr.io/celonis/celostar/starrocks-dev-env:{{ env.dev_env_branch }}`