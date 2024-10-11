#!/bin/bash
set -e

GITHUB_API_URL="${API_URL:-https://api.github.com}"
GITHUB_SERVER_URL="${SERVER_URL:-https://github.com}"
INPUT_GITHUB_USER=celobot
wait_interval=60
propagate_failure=true

# Get the info of workflow actions.
api() {
  path="$1"

  response=$(curl --fail-with-body -sSL \
    "${GITHUB_API_URL}/repos/${INPUT_OWNER}/${INPUT_REPO}/actions/$path" \
    -H "Authorization: Bearer ${GITHUB_TOKEN}" \
    -H 'Accept: application/vnd.github.v3+json' \
    -H 'Content-Type: application/json')

  curl_exit_status=$?

  if [ $curl_exit_status -eq 0 ]; then
    echo "$response"
  else
    echo >&2 "api failed:"
    echo >&2 "path: $path"
    echo >&2 "response: $response"
    if [[ "$response" == *'"Server Error"'* ]]; then
      echo >&2 "Server error - trying again"
    else
      exit 1
    fi
  fi
}

# Trigger a workflow using "repository_dispatch" event.
repository_dispatch_api() {
  echo "Start to trigger a workflow ..."
  response=$(curl -s -w "%{http_code}" -L -X POST \
      "${GITHUB_API_URL}/repos/${INPUT_OWNER}/${INPUT_REPO}/dispatches" \
      -H "Authorization: Bearer ${GITHUB_TOKEN}" \
      -H 'Accept: application/vnd.github+json' \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      "$@")

  echo "The response status code: $response"
  if [ "$response" -ne 204 ]; then
    echo "Something is wrong!"
    exit 1
  fi
}

lets_wait() {
  local interval=${1:-$wait_interval}
  echo "Sleeping for $interval seconds"
  sleep "$interval"
}

# Return the ids of the latest repository dispatched workflow runs.
get_workflow_runs() {
  since="$1"
  echo "Getting run ids since $since" >&2

  query="event=repository_dispatch&created=>=$since${INPUT_GITHUB_USER+&actor=}${INPUT_GITHUB_USER}&per_page=10"

  api "workflows/${INPUT_WORKFLOW_FILE_NAME}/runs?${query}" | jq -r '.workflow_runs[].id' | sort
}

wait_for_workflow_to_finish() {
  last_workflow_id="$1"
  last_workflow_url="${GITHUB_SERVER_URL}/${INPUT_OWNER}/${INPUT_REPO}/actions/runs/${last_workflow_id}"

  echo "Waiting for workflow to finish:"
  echo "The workflow id is [${last_workflow_id}]."
  echo "The workflow logs can be found at ${last_workflow_url}"
  echo "workflow_id=${last_workflow_id}" >> $GITHUB_OUTPUT
  echo "workflow_url=${last_workflow_url}" >> $GITHUB_OUTPUT
  echo ""

  conclusion=null
  status=

  while [[ "${conclusion}" == "null" && "${status}" != "completed" ]]
  do
    lets_wait

    workflow=$(api "runs/$last_workflow_id")
    conclusion=$(echo "${workflow}" | jq -r '.conclusion')
    status=$(echo "${workflow}" | jq -r '.status')

    echo "Checking conclusion [${conclusion}]"
    echo "Checking status [${status}]"
    echo "conclusion=${conclusion}" >> $GITHUB_OUTPUT
  done

  if [[ "${conclusion}" == "success" && "${status}" == "completed" ]]
  then
    echo "Yes, success"
  else
    # Alternative "failure"
    echo "Conclusion is not success, it's [${conclusion}]."

    if [ "${propagate_failure}" = true ]
    then
      echo "Propagating failure to upstream job"
      exit 1
    fi
  fi
}

trigger_workflow_and_wait() {
  START_TIME=$(date +%s)
  SINCE=$(date -u -Iseconds -d "@$((START_TIME - 120))") # Two minutes ago, to overcome clock skew

  OLD_RUNS=$(get_workflow_runs "$SINCE")

  client_payload=$(echo "${INPUT_CLIENT_PAYLOAD}" | jq -c)

  echo "Triggering workflow:"
  echo "  ${client_payload}"

  repository_dispatch_api -d "${client_payload}"

  NEW_RUNS=$OLD_RUNS
  while [ "$NEW_RUNS" = "$OLD_RUNS" ]
  do
    NEW_RUNS=$(get_workflow_runs "$SINCE")
    lets_wait
    echo "NEW_RUNS: $NEW_RUNS"
  done

  echo "OLD_RUNS: $OLD_RUNS"
  echo "NEW_RUNS: $NEW_RUNS"

  # Use temporary files for join
  old_runs_file=$(mktemp)
  new_runs_file=$(mktemp)

  echo "$OLD_RUNS" > "$old_runs_file"
  echo "$NEW_RUNS" > "$new_runs_file"

  # Return new run ids
  run_ids=$(join -v2 "$old_runs_file" "$new_runs_file")

  echo "Will wait for the completion of: $run_ids"

  # Clean up temporary files
  rm "$old_runs_file" "$new_runs_file"

  for run_id in $run_ids
  do
    echo "Waiting for the completion of run: $run_id"
    wait_for_workflow_to_finish "$run_id"
  done
}



main() {
  INPUT_OWNER="$1"
  INPUT_REPO="$2"
  INPUT_WORKFLOW_FILE_NAME="$3"
  INPUT_CLIENT_PAYLOAD="$4"

  echo "Running a workflow with: "
  echo "  INPUT_OWNER: $INPUT_OWNER"
  echo "  INPUT_REPO: $INPUT_REPO"
  echo "  INPUT_WORKFLOW_FILE_NAME: $INPUT_WORKFLOW_FILE_NAME"
  echo "  INPUT_CLIENT_PAYLOAD: $INPUT_CLIENT_PAYLOAD"

  trigger_workflow_and_wait
}

main "$1" "$2" "$3" "$4"
