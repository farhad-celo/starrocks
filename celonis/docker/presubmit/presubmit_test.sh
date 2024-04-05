#!/usr/bin/env bash

set -e

cd /celostar-starrocks;

#echo "./build.sh --fe --clean";
#./build.sh --fe --clean;
#
#echo "./build.sh --be --clean -j `nproc`";
#./build.sh --be --clean -j `nproc`;

echo "Start to run FE UT"
export FE_UT_PARALLEL=16;
./run-fe-ut.sh;

echo "Start to run Celonis BE UT"
export LD_LIBRARY_PATH=/var/local/thirdparty/installed/lib
./run-be-ut.sh --use-staros --clean -j `nproc` --test "Celonis*"
