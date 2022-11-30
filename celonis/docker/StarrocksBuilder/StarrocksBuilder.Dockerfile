# CODE_SOURCE: [upstream, celostar, local]
# upstream: use upstream starrocks repo to build.
# celostar: use celostar-starrocks repo to build.
# local: use a local repo to build. WARNING: this should only be used for test, NOT for production release
ARG CODE_SOURCE=celostar

# dev-env branch.
# Refer to https://hub.docker.com/r/starrocks/dev-env for the tag definition
ARG BRANCH=branch-2.5

# Starrocks github version tag
ARG STARROCKS_VERSION=0.0.0-pr43-sr2.5-70b71794

# The artifact want to build and release: [fe, be, broker, all, copylocal]
# fe: build only frontend artifacts
# be: build only backend artifacts
# broker: build only broker artifacts
# all: build all three components(fe, be, broker) artifacts
# copylocal: not build, just copy the already built artifacts from a local repo. WARNING: this should only be used for test, NOT for production release
ARG ARTIFACT_TYPE=all

# the path to the local starrocks repo in relative to the context folder.
# Recommend having only one local repo under the context folder to minimize the file copy for setting up the docker context.
ARG LOCAL_REPO_PATH=./

# Normalize code carrier
FROM busybox:latest as upstream
# For upstream CODE_SOURCE, it will download the corresponding released code from https://github.com/StarRocks/starrocks/releases
ARG STARROCKS_VERSION
WORKDIR /code
RUN wget https://github.com/StarRocks/starrocks/archive/refs/tags/${STARROCKS_VERSION}.tar.gz && \
    tar zxf ${STARROCKS_VERSION}.tar.gz && rm ${STARROCKS_VERSION}.tar.gz && \
    mv starrocks-${STARROCKS_VERSION} starrocks

FROM busybox:latest as celostar
# For celostar CODE_SOURCE, it will download the corresponding released code from https://github.com/celonis/celostar-starrocks/tags . Since celostar-starrock is a private repo, a github token is required to proceed.
ARG STARROCKS_VERSION
ARG GITHUB_TOKEN
WORKDIR /code
RUN wget --header="Authorization: token ${GITHUB_TOKEN}" \
      https://github.com/celonis/celostar-starrocks/archive/refs/tags/${STARROCKS_VERSION}.tar.gz && \
    tar zxf ${STARROCKS_VERSION}.tar.gz && rm ${STARROCKS_VERSION}.tar.gz && \
    mv celostar-starrocks-${STARROCKS_VERSION} starrocks

FROM busybox:latest as local
# For local CODE_SOURCE, it will copy the code from a local repo path to proceed. The local repo code doesn't necessarily need to be commited or merged to the remote repo.
ARG LOCAL_REPO_PATH
WORKDIR /code
COPY ${LOCAL_REPO_PATH} ./starrocks

FROM ${CODE_SOURCE} as code


# Normalize builder
FROM 904263465335.dkr.ecr.us-west-1.amazonaws.com/celostar-dev-env:${BRANCH} as builder

FROM builder as fe-builder
# clean and build Frontend and Spark Dpp application
COPY --from=code /code /build
WORKDIR /build/starrocks
RUN ./build.sh --fe --clean

FROM builder as be-builder
# build Backend in different mode (build_type could be Release, Debug, or Asan. Default value is Release.
ARG BUILD_TYPE=Release

COPY --from=code /code /build
WORKDIR /build/starrocks
RUN BUILD_TYPE=${BUILD_TYPE} ./build.sh --be --clean -j `nproc`

FROM builder as broker-builder
# clean and build Frontend and Spark Dpp application
COPY --from=code /code /build
WORKDIR /build/starrocks/fs_brokers/apache_hdfs_broker
RUN ./build.sh


FROM busybox:latest as all-artifacts
WORKDIR release
COPY --from=fe-builder /build/starrocks/output fe_artifacts
COPY --from=be-builder /build/starrocks/output be_artifacts
COPY --from=broker-builder /build/starrocks/fs_brokers/apache_hdfs_broker/output broker_artifacts

FROM busybox:latest as fe-artifacts
WORKDIR release
COPY --from=fe-builder /build/starrocks/output fe_artifacts

FROM busybox:latest as be-artifacts
WORKDIR release
COPY --from=be-builder /build/starrocks/output be_artifacts

FROM busybox:latest as broker-artifacts
WORKDIR release
COPY --from=broker-builder /build/starrocks/fs_brokers/apache_hdfs_broker/output broker_artifacts

FROM busybox:latest as copylocal-artifacts
ARG LOCAL_REPO_PATH
WORKDIR release
COPY ${LOCAL_REPO_PATH}/output/fe fe_artifacts/fe
COPY ${LOCAL_REPO_PATH}/output/be be_artifacts/be
COPY ${LOCAL_REPO_PATH}/output/udf be_artifacts/udf
COPY ${LOCAL_REPO_PATH}/fs_brokers/apache_hdfs_broker/output broker_artifacts

FROM ${ARTIFACT_TYPE}-artifacts as artifacts
