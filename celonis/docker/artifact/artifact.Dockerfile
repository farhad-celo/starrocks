# This docker file build the Starrocks artifacts fe/be/udfs and package them into a busybox basedimage
# Please run this command from the git repo root directory to build:
# DOCKER_BUILDKIT=1 docker build --rm=true -f celonis/docker/StarrocksBuilder/StarrocksBuilder.Dockerfile -t starrocks-artifacts:tag .

# dev-env image, replace it with ghcr.io/celonis/celostar/starrocks-centos-dev-env:latest to build centos artifacts
ARG builder=ghcr.io/starrocks/starrocks/dev-env-ubuntu:main
ARG RELEASE_VERSION

FROM ${builder} as fe-builder
ARG RELEASE_VERSION
# clean and build Frontend and Spark Dpp application
COPY . /build/starrocks
WORKDIR /build/starrocks
RUN STARROCKS_VERSION=${RELEASE_VERSION} MAVEN_OPTS='-Dmaven.artifact.threads=128' ./build.sh --fe --clean

FROM ${builder} as be-builder
ARG RELEASE_VERSION
# build Backend in different mode (build_type could be Release, Debug, or Asan. Default value is Release.
ARG BUILD_TYPE=Release
COPY . /build/starrocks
WORKDIR /build/starrocks
RUN STARROCKS_VERSION=${RELEASE_VERSION} BUILD_TYPE=${BUILD_TYPE} ./build.sh --be --clean -j `nproc`

FROM ${builder} as udf-builder
# clean and build duplicate-invoice-checker UDF
COPY . /build/starrocks
WORKDIR /build/starrocks/celonis/udf/duplicate-invoice-checker
RUN MAVEN_OPTS='-Dmaven.artifact.threads=128' mvn package

FROM busybox:latest
LABEL org.opencontainers.image.source = "https://github.com/celonis/celostar-starrocks"

COPY --from=fe-builder /build/starrocks/output /release/fe_artifacts
COPY --from=be-builder /build/starrocks/output /release/be_artifacts
COPY --from=udf-builder /build/starrocks/celonis/udf/duplicate-invoice-checker/target/duplicate-invoice-checker-udf-1.0-SNAPSHOT-jar-with-dependencies.jar /release/udf/duplicate-invoice-checker-udf-1.0-SNAPSHOT.jar

# Add azure bolbstore dependencies.
# TODO: remove this when StarRocks team having resolved these two issues:
#   https://github.com/StarRocks/starrocks/issues/18341
#   https://github.com/StarRocks/starrocks/issues/18342
ARG BE_AZURE_DIR=/release/be_artifacts/be/lib/hadoop/common
ARG FE_AZURE_DIR=/release/fe_artifacts/fe/lib
ADD https://repo1.maven.org/maven2/org/apache/hadoop/hadoop-azure-datalake/3.3.0/hadoop-azure-datalake-3.3.0.jar \
    https://repo1.maven.org/maven2/org/apache/hadoop/hadoop-azure/3.3.0/hadoop-azure-3.3.0.jar \
    https://repo1.maven.org/maven2/com/microsoft/azure/azure-storage/8.6.6/azure-storage-8.6.6.jar \
    $FE_AZURE_DIR/
ADD https://repo1.maven.org/maven2/org/apache/hadoop/hadoop-azure-datalake/3.3.0/hadoop-azure-datalake-3.3.0.jar \
    https://repo1.maven.org/maven2/org/apache/hadoop/hadoop-azure/3.3.0/hadoop-azure-3.3.0.jar \
    https://repo1.maven.org/maven2/com/microsoft/azure/azure-storage/8.6.6/azure-storage-8.6.6.jar \
    $BE_AZURE_DIR/
COPY celonis/docker/artifact/core-site.xml /release/fe_artifacts/fe/conf/
COPY celonis/docker/artifact/core-site.xml /release/be_artifacts/be/conf/

WORKDIR /release
