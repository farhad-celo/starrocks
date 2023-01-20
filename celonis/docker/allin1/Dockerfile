# ARTIFACTIMAGE is the image of the artifacts that this all-in-one runtime docker will use.
# It can be a docker image from any registry or just a image that built locally
ARG ARTIFACTIMAGE=904263465335.dkr.ecr.us-west-1.amazonaws.com/starrocks-artifacts:0.0.0-pr43-sr2.5-70b71794

ARG cgversion=cg1

FROM ${ARTIFACTIMAGE} as artifacts

FROM centos:7 as dependencies-installed
# Install Java JDK.
RUN yum -y install java-1.8.0-openjdk-devel.x86_64

# Install relevant tools.
RUN yum -y install mysql net-tools telnet tree perf

FROM dependencies-installed as artifacts-deployed

ARG DEPLOYDIR="/data/deploy"

# STARTMODE: [auto, manual]
ENV STARTMODE=manual

ENV JAVA_HOME=/usr/lib/jvm/java-1.8.0
ENV SR_HOME=/data/deploy/starrocks

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

FROM artifacts-deployed as runtime_cg1

FROM artifacts-deployed as runtime_cg2
# Note: this is to address the bug introduced by this PR: https://github.com/StarRocks/starrocks/pull/11717#issuecomment-1316005178
# We should remove copying this file when it the bug is fixed by upstreamCOPY common.sh $DEPLOYDIR/starrocks/be/bin/common.sh
COPY common.sh $DEPLOYDIR/starrocks/fe/bin/common.sh
COPY common.sh $DEPLOYDIR/starrocks/be/bin/common.sh

FROM runtime_${cgversion}
CMD if [ "$STARTMODE" = 'auto' ] ; then ./start_fe_be.sh ; else ./start_be.sh ; fi
