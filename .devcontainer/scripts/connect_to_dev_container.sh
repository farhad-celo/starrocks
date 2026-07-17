#!/usr/bin/env bash

# change the directory to the scripts so that everything can be relative.
cd "$(dirname "$0")"

function log() {
  echo "------------------------------------------------------------------------"
  echo $1
  echo "------------------------------------------------------------------------"
}
# Ensure the container is stopped when the script exits or is interrupted
trap 'log "Stopping docker container"; docker stop "${container_id}" >/dev/null 2>&1' EXIT INT TERM

log "Starting docker container"

container_id=$(./build_and_start_dev_container.sh)

docker exec -it ${container_id} /bin/bash
