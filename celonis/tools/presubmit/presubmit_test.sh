#!/usr/bin/env bash


# Make sure any single failure will fail the whole script
set -e

cd /celostar-starrocks;

#echo "./build.sh --fe --clean";
#./build.sh --fe --clean;
#
#echo "./build.sh --be --clean -j `nproc`";
#./build.sh --be --clean -j `nproc`;

# Currently FE UT is taking 1+ hour due to installing jars and we don't do any development on it.
# TODO: reinstall FE UT if necessary
#echo "Start to run FE UT"
#export FE_UT_PARALLEL=16;
#./run-fe-ut.sh;

echo "Checking formatting on Celonis subdirs"
if ! ./build-support/check-format-celonis.sh; then
    echo "Some source files are not formatted correctly. Please use 'build-support/clang-format-celonis.sh' to format them."
    exit 1
fi

echo "Start to run BE UT"
export LD_LIBRARY_PATH=/var/local/thirdparty/installed/lib
export GTEST_PARALLEL=celonis/tools/gtest-parallel/gtest-parallel
export PYTHON=python3

./run-be-ut.sh -j `nproc` --clean
