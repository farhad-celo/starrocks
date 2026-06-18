#!/usr/bin/env bash

# default config -c
config=""
# default workspace_folder -w
workspace_folder=$(realpath ../)
# default preset -p
preset=Debug

# override defaults when specified
while getopts 'c:w:p:' flag; do
  case "${flag}" in
  c) config="${OPTARG}" ;;
  w) workspace_folder="${OPTARG}" ;;
  p) preset="${OPTARG}" ;;
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

if [[ $config != "" ]]; then
  container_id=$(./build_and_start_dev_container.sh -c "${config}" -w "${workspace_folder}")
else
  # use the default -c of the ./build_and_start_dev_container.sh
  container_id=$(./build_and_start_dev_container.sh -w "${workspace_folder}")
fi

log "Start compilation"
docker exec "${container_id}" bash -lc 'cd /workspaces/celostar-starrocks/be && cmake --preset $1 && cmake --build --preset $1' -- "${preset}"
