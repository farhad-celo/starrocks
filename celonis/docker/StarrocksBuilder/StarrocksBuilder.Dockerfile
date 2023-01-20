ARG builder=ghcr.io/celonis/celostar/starrocks-dev-env:branch-2.5

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
COPY --from=fe-builder /build/starrocks/output /release/fe_artifacts
COPY --from=be-builder /build/starrocks/output /release/be_artifacts
COPY --from=udf-builder /build/starrocks/celonis/udf/duplicate-invoice-checker/target/duplicate-invoice-checker-udf-1.0-SNAPSHOT-jar-with-dependencies.jar /release/udf/duplicate-invoice-checker-udf-1.0-SNAPSHOT.jar

WORKDIR /release
