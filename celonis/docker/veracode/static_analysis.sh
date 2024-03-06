#!/usr/bin/env bash

set -e

cd /celostar-starrocks;

echo "./build.sh --fe --clean";
./build.sh --fe --clean;

# Zip FE
echo "Start to zip FE jars"
zip celostar-starrocks-fe.zip ./output/fe/lib/*.jar
