#!/usr/bin/env bash

# change the directory to the scripts so that everything can be relative.
cd "$(dirname "$0")"

# default config -c
config="../devcontainer.json"
# default workplace_folder -w
workspace_folder=$(realpath ../../)

# override defaults when specified
while getopts 'c:w:' flag; do
  case "${flag}" in
  c) config="${OPTARG}" ;;
  w) workspace_folder="${OPTARG}" ;;
  *)
    exit 1
    ;;
  esac
done

function run() {
  output=$(devcontainer build --workspace-folder "${workspace_folder}" --config "${config}")
  output=$(devcontainer up --workspace-folder "${workspace_folder}" --config "${config}")
  container_id=$(echo ${output} | jq -r '.containerId')
  echo ${container_id}
}
# return value via echo
echo $(run)
