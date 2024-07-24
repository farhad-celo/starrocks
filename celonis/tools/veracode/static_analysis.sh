#!/usr/bin/env bash

set -e

cd /celostar-starrocks;

echo "./build.sh --fe --clean";
./build.sh --fe --clean;

echo "./build.sh --be --without-starcache --with-brpc-keepalive --clean -j `nproc`";
./build.sh --be --without-starcache --with-brpc-keepalive --clean -j `nproc`;

echo "mkdir jars_for_static_scan"
mkdir jars_for_static_scan

# Zip FE
echo "Start to zip jars in output"
find ./output -name '*.jar' -exec mv {} jars_for_static_scan \;
