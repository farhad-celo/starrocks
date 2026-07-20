#!/usr/bin/env sh

#Utility to run any command within the starrocks dev image.

set -e

# To update this, find the dev-env version that matches to the base branch version.
# https://hub.docker.com/r/starrocks/dev-env/tags
STARROCKS_DEV_IMAGE=starrocks/dev-env:branch-2.5
STARROCKS_CONTAINER_PATH=/root/starrocks
MY_SUBDIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
STARROCKS_SUBDIR="$(dirname "$MY_SUBDIR")"
STARROCKS_VOLUME=$STARROCKS_SUBDIR:$STARROCKS_CONTAINER_PATH
echo $STARROCKS_VOLUME
docker run -i --rm -w $STARROCKS_CONTAINER_PATH -v $STARROCKS_VOLUME -v $HOME/.starrocksm2:/root/.m2 -e ENABLE_QUERY_DEBUG_TRACE=ON $STARROCKS_DEV_IMAGE "$@"
