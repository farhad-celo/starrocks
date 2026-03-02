#!/usr/bin/env bash

# default config -c
config=""
# default workspace_folder -w
workspace_folder=$(realpath ../)
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
  c) config="${OPTARG}" ;;
  w) workspace_folder="${OPTARG}" ;;
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

if [[ $config != "" ]]; then
  container_id=$(./build_and_start_dev_container.sh -c "${config}" -w "${workspace_folder}")
else
  # use the default -c of the ./build_and_start_dev_container.sh
  container_id=$(./build_and_start_dev_container.sh -w "${workspace_folder}")
fi

log "Start compilation"
docker exec "${container_id}" bash -lc 'cd /root/celostar-starrocks && BUILD_TYPE="$1" ./build.sh $2 -j "$3"' -- "${build_type}" "${options}" "${jobs}"
