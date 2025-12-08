#!/usr/bin/env bash
set -eo pipefail

echo "Executing check-format-celonis.sh"

ROOT=`dirname "$0"`
ROOT=`cd "$ROOT"; pwd`

export STARROCKS_HOME=`cd "${ROOT}/.."; pwd`

CLANG_FORMAT=${CLANG_FORMAT_BINARY:=$(which clang-format)}

python3 ${STARROCKS_HOME}/build-support/run_clang_format.py --clang_format_binary="${CLANG_FORMAT}" \
	--source_dirs="${STARROCKS_HOME}/be/src/exprs/celonis","${STARROCKS_HOME}/be/test/exprs/celonis,${STARROCKS_HOME}/be/src/bench/celonis" \
        --exclude_globs="${STARROCKS_HOME}/build-support/excludes" --quiet


