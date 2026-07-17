#!/usr/bin/env bash

# change the directory to the scripts so that everything can be relative.
cd "$(dirname "$0")"


# default build_type -b
build_type=Debug
# default options -o
options="--be --fe"
# default number of jobs -j
jobs=60
#

# override defaults when specified
while getopts 'c:w:b:o:' flag; do
  case "${flag}" in
  b) build_type="${OPTARG}" ;;
  o) options="${OPTARG}" ;;
  j) jobs="${OPTARG}" ;;
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
docker exec "${container_id}" bash -lc 'cd /workspaces/celostar-starrocks && BUILD_TYPE="$1" ./build.sh $2 -j "$3"' -- "${build_type}" "${options}" "${jobs}"
