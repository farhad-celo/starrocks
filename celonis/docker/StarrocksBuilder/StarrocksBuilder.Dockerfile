# This docker file build the Starrocks artifacts fe/be/udfs and package them into a busybox basedimage
# Please run this command from the git repo root directory to build:
# DOCKER_BUILDKIT=1 docker build --rm=true -f celonis/docker/StarrocksBuilder/StarrocksBuilder.Dockerfile -t starrocks-artifacts:tag .

ARG builder=ghcr.io/celonis/celostar/starrocks-dev-env:3.5.6-0930

FROM ${builder} as fe-builder
# clean and build Frontend and Spark Dpp application
COPY . /build/starrocks
WORKDIR /build/starrocks
RUN MAVEN_OPTS='-Dmaven.artifact.threads=128' ./build.sh --fe --clean

FROM ${builder} as be-builder
# build Backend in different mode (build_type could be Release, Debug, or Asan. Default value is Release.
ARG BUILD_TYPE=Release
COPY . /build/starrocks
WORKDIR /build/starrocks
RUN BUILD_TYPE=${BUILD_TYPE} ./build.sh --be --clean -j `nproc`

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

WORKDIR /release
