#!/usr/bin/env bash

# default config -c
config="./arm/devcontainer.json"
arch=$(uname -m)
if [[ $arch == x86_* ]]; then
  config="./x86/devcontainer.json"
fi
# default workplace_folder -w
workspace_folder=$(realpath ../)

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
