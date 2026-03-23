#!/usr/bin/env bash
set -eo pipefail

ROOT=`dirname "$0"`
ROOT=`cd "$ROOT"; pwd`

export STARROCKS_HOME=`cd "${ROOT}/.."; pwd`

error_handler() {
    local line_number=$1
    local error_code=$2
    local command="$3"

    echo "ERROR at line $line_number: '$command' failed with code $error_code" >&2
}

trap 'error_handler ${LINENO} $? "$BASH_COMMAND"' ERR

# If this fails, you likely don't have a symbolic link for clang-format (maybe you need to create a link to e.g., clang-format-17)
CLANG_FORMAT=${CLANG_FORMAT_BINARY:=$(which clang-format)}

python3 ${STARROCKS_HOME}/build-support/run_clang_format.py --clang_format_binary="${CLANG_FORMAT}" --fix \
        --source_dirs="${STARROCKS_HOME}/be/src/exprs/celonis","${STARROCKS_HOME}/be/test/exprs/celonis,${STARROCKS_HOME}/be/src/bench/celonis" \
        --exclude_globs="${STARROCKS_HOME}/build-support/excludes"


