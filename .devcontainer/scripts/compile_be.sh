#!/usr/bin/env bash

# change the directory to the scripts so that everything can be relative.
cd "$(dirname "$0")"

# default preset -p
preset=Debug

# override defaults when specified
while getopts 'c:w:p:' flag; do
  case "${flag}" in
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

container_id=$(./build_and_start_dev_container.sh)

log "Start compilation"
docker exec "${container_id}" bash -lc 'cd /workspaces/celostar-starrocks/be && cmake --preset $1 && cmake --build --preset $1' -- "${preset}"
