# This file is based on the instructions here except that rather than download
# binary, we instead use the output of the build.
# https://docs.starrocks.com/en-us/latest/quick_start/Deploy#create-dockerfile

FROM centos:centos7

# Copy build output into place
COPY output/ /data/deploy/starrocks

# Note: this is to address the bug introduced by this PR: https://github.com/StarRocks/starrocks/pull/11717#issuecomment-1316005178
# We should remove copying this file when it the bug is fixed by upstreamCOPY common.sh $DEPLOYDIR/starrocks/be/bin/common.sh
COPY celonis/common.sh /data/deploy/starrocks/fe/bin/common.sh
COPY celonis/common.sh /data/deploy/starrocks/be/bin/common.sh

# Prepare StarRocks Installer.
RUN yum -y install wget
RUN mkdir -p /data/deploy/
RUN cd /data/deploy/

# Install Java JDK.
RUN yum -y install java-1.8.0-openjdk-devel.x86_64
RUN rpm -ql java-1.8.0-openjdk-devel.x86_64 | grep bin$
RUN /usr/lib/jvm/java-1.8.0/bin/java -version

# Create directory for FE meta and BE storage in StarRocks.
RUN mkdir -p /data/deploy/starrocks/fe/meta
RUN jps
RUN mkdir -p /data/deploy/starrocks/be/storage

# Install relevant tools.
RUN yum -y install mysql net-tools telnet

# Run Setup script.
ARG STARROCKS_RUN_SCRIPT
COPY $STARROCKS_RUN_SCRIPT /data/deploy/run_script.sh
RUN chmod +x /data/deploy/run_script.sh
CMD /data/deploy/run_script.sh
