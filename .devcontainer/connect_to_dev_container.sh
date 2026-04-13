#!/usr/bin/env bash

# default config -c
config="./x86/devcontainer.json"
# default workspace_folder -w
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

function log() {
  echo "------------------------------------------------------------------------"
  echo $1
  echo "------------------------------------------------------------------------"
}
# Ensure the container is stopped when the script exits or is interrupted
trap 'log "Stopping docker container"; docker stop "${container_id}" >/dev/null 2>&1' EXIT INT TERM

log "Starting docker container"

container_id=$(./build_and_start_dev_container.sh -c "${config}" -w "${workspace_folder}")

docker exec -it ${container_id} /bin/bash
