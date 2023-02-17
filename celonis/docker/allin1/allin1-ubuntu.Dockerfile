# Dockerfile for building a starrocks allin1 image
# Please run this command from the git repo root dir to build the image
#   DOCKER_BUILDKIT=1 docker build --build-arg ARTIFACTIMAGE=starrocks-artifacts:${RELEASE_VERSION} -f celonis/docker/allin1/Dockerfile -t starrocks-allin1:${RELEASE_VERSION} celonis/docker/allin1

# ARTIFACTIMAGE is the image of the artifacts that this all-in-one runtime docker will use.
# It can be a docker image from any registry or just a image that built locally
ARG ARTIFACTIMAGE=artifact-ubuntu:latest

FROM ${ARTIFACTIMAGE} as artifacts

FROM ubuntu:22.04 as dependencies-installed

LABEL org.opencontainers.image.source="https://github.com/celonis/celostar-starrocks"


RUN apt-get update -y \
        && apt-get install -y --no-install-recommends binutils-dev default-jdk python2 \
           mysql-client curl vim tree net-tools \
           linux-tools-common linux-tools-generic \
        && rm -rf /var/lib/apt/lists/*

RUN echo "export PATH=/usr/lib/linux-tools/5.15.0-60-generic:$PATH" >> /root/.bashrc

ENV JAVA_HOME=/lib/jvm/default-java
ENV SR_HOME=/data/deploy/starrocks
# STARTMODE: [auto, manual]
ENV STARTMODE=manual

ARG DEPLOYDIR=/data/deploy

WORKDIR $DEPLOYDIR

# Copy all artifacts to the runtime container image
COPY --from=artifacts /release/be_artifacts/ $DEPLOYDIR/starrocks
COPY --from=artifacts /release/fe_artifacts/ $DEPLOYDIR/starrocks
COPY --from=artifacts /release/udf/ $DEPLOYDIR/starrocks/udf/

# Create directory for FE meta and BE storage in StarRocks.
RUN mkdir -p $DEPLOYDIR/starrocks/fe/meta && mkdir -p $DEPLOYDIR/starrocks/be/storage

# Copy Setup script.
COPY *.sh $DEPLOYDIR
RUN chmod +x *.sh

COPY *.conf $DEPLOYDIR
RUN cat be.conf >> $DEPLOYDIR/starrocks/be/conf/be.conf && \
    cat fe.conf >> $DEPLOYDIR/starrocks/fe/conf/fe.conf


CMD if [ "$STARTMODE" = 'auto' ] ; then ./start_fe_be.sh ; else ./start_be.sh ; fi

