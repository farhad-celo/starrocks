ARG builder=904263465335.dkr.ecr.us-west-1.amazonaws.com/celostar-dev-env:branch-2.5

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

FROM ${builder} as broker-builder
# clean and build Frontend and Spark Dpp application
COPY . /build/starrocks
WORKDIR /build/starrocks/fs_brokers/apache_hdfs_broker
RUN MAVEN_OPTS='-Dmaven.artifact.threads=128' ./build.sh

FROM busybox:latest
COPY --from=fe-builder /build/starrocks/output /release/fe_artifacts
COPY --from=be-builder /build/starrocks/output /release/be_artifacts
COPY --from=broker-builder /build/starrocks/fs_brokers/apache_hdfs_broker/output /release/broker_artifacts
WORKDIR /release
