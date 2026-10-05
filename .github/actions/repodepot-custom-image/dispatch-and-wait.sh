#!/bin/bash

set -euo pipefail

if [[ -z "${GH_REPO:-}" ]]; then
    echo "::error::GH_REPO must be set before dispatching a workflow."
    exit 1
fi

dispatch_output=
if ! dispatch_output=$(gh workflow run "$@" 2>&1); then
    echo "$dispatch_output"
    exit 1
fi
echo "$dispatch_output"

# gh workflow run returns the URL of the workflow run it created. Use that URL
# instead of querying the most recent run, which can return stale data while the
# Actions API is still indexing the new run.
run_url=$(grep -Eo 'https://[^[:space:]]+/actions/runs/[0-9]+' <<< "$dispatch_output" | tail -1 || true)
run_id=${run_url##*/}

if [[ ! "$run_id" =~ ^[0-9]+$ ]]; then
    echo "::error::Could not determine the run ID from the workflow dispatch output."
    exit 1
fi

echo "Waiting for workflow run: $run_url"
gh run watch "$run_id" --repo "$GH_REPO" -i 300 --exit-status
