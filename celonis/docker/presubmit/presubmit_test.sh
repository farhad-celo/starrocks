#!/usr/bin/env bash

set -e

cd /celostar-starrocks;

echo "./build.sh --fe --clean";
./build.sh --fe --clean;

echo "./build.sh --be --clean -j `nproc`";
./build.sh --be --clean -j `nproc`;

# Put FE & BE UT back after migrating to branch-3.2

#echo "./run-be-ut.sh";
#./run-be-ut.sh --use-staros --clean -j `nproc`;

#echo "./run-fe-ut.sh";
#export FE_UT_PARALLEL=32;
#./run-fe-ut.sh;
