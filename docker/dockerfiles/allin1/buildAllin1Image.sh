#!/bin/bash

set -e

RELEASE_VERSION=${RELEASE_VERSION:-"latest"}
BUILD_TYPE=${BUILD_TYPE:-"Release"}
BUILDER=${BUILDER:-"ghcr.io/celonis/celostar/starrocks-dev-env:3.5-latest"}
DD_AGENT_VERSION=${DD_AGENT_VERSION:-"1.56.3"}

docker pull $BUILDER
docker buildx build --force-rm=true --build-arg RELEASE_VERSION=${RELEASE_VERSION} --build-arg BUILD_TYPE=${BUILD_TYPE} --build-arg DD_AGENT_VERSION=${DD_AGENT_VERSION} --build-arg builder=${BUILDER} -f ../artifacts/artifact.Dockerfile -t starrocks-artifacts:${RELEASE_VERSION} ../../..
docker buildx build --force-rm=true --build-arg ARTIFACTIMAGE=starrocks-artifacts:${RELEASE_VERSION} -f allin1-ubuntu.Dockerfile -t ghcr.io/celonis/celostar/starrocks-allin1:${RELEASE_VERSION} ../../..
