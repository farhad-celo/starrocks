# ARTIFACTIMAGE is the image of the artifacts that this all-in-one runtime docker will use.
# It can be a docker image from any registry or just a image that built locally
ARG ARTIFACTIMAGE=artifact-centos:latest


FROM ${ARTIFACTIMAGE} as artifacts

FROM centos:7 as dependencies-installed

LABEL org.opencontainers.image.source="https://github.com/celonis/celostar-starrocks"

# Install Java JDK.
RUN yum -y install java-1.8.0-openjdk-devel.x86_64

# Install relevant tools.
RUN yum -y install mysql net-tools telnet tree perf

ENV JAVA_HOME=/usr/lib/jvm/java-1.8.0
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
