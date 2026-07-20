#!/usr/bin/env sh

set -e
MY_SUBDIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
$MY_SUBDIR/run-in-dev-image.sh ./build.sh
STARROCKS_SUBDIR="$(dirname "$MY_SUBDIR")"
MY_SUBDIR_RELATIVE="$(basename "$MY_SUBDIR")"
docker build --build-arg STARROCKS_RUN_SCRIPT="./$MY_SUBDIR_RELATIVE/run_script.sh" --no-cache --progress=plain -f $MY_SUBDIR/Dockerfile $STARROCKS_SUBDIR "$@"
