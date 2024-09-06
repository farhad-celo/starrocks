#!/usr/bin/env bash

set -e

cd /celostar-starrocks;

echo "./build.sh --fe --clean";
BUILD_TYPE=Release MAVEN_OPTS="-Dmaven.artifact.threads=128" ./build.sh --fe --clean;

#echo "./build.sh --be --clean -j `nproc`";
#BUILD_TYPE=Release MAVEN_OPTS="-Dmaven.artifact.threads=128" ./build.sh --be --clean -j `nproc`;

echo "mkdir jars_for_static_scan"
mkdir jars_for_static_scan

# Zip FE
echo "Start to zip jars in output"
find ./output -name '*.jar' -exec mv {} jars_for_static_scan \;
