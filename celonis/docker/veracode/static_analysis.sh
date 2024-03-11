#!/usr/bin/env bash

set -e

cd /celostar-starrocks;

echo "./build.sh --fe --clean";
./build.sh --fe --clean;

echo "./build.sh --be --clean -j `nproc`";
./build.sh --be --clean -j `nproc`;

# Zip FE
echo "Start to zip jars in output"
find ./output -name '*.jar' -print -exec zip celostar-starrocks-java.zip {} +
