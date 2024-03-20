# StarRocks Dev Env Image
This directory contains dockerfiles to build the docker image for the development environment.

A StarRocks development environment contains all necessary development tools installed as well as prebuilt StarRocks third
party dependencies.

## 1 Build dev env image

### 1.1 Build ubuntu dev env image
```
DOCKER_BUILDKIT=1 docker buildx build --rm=true -f dev-env.Dockerfile -t ghcr.io/OWNER/starrocks/dev-env-ubuntu:<tag> ../../..
```
E.g.:
```shell
DOCKER_BUILDKIT=1 docker buildx build --platform=linux/amd64 --rm=true -f dev-env.Dockerfile -t ghcr.io/celonis/celostar/starrocks-dev-env:mac ../../..
```

