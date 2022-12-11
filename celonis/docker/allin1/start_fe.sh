#!/bin/bash

# Set JAVA_HOME.
export JAVA_HOME=/usr/lib/jvm/java-1.8.0

export SR_HOME=/data/deploy/starrocks

# Start FE.
cd $SR_HOME/fe/bin/

# Try stop fe first if there is a running instance
./stop_fe.sh
sleep 3

# enable [FQDN access](https://docs.starrocks.io/en-us/2.4/administration/enable_fqdn#enable-fqdn-access)
./start_fe.sh --host_type FQDN --daemon

sleep 30;

# TODO(j.yang): Explicitly set pipeline_sink_dop because pipeline load currently only
# uses parallelism 1 by default. Remove this after automatic parallelism selection
# works.
MYFQDN=`hostname --fqdn`
SINK_DOP=$(($(nproc) / 2))
mysql -uroot -h${MYFQDN} -P 9030 -e "set global pipeline_sink_dop=${SINK_DOP};"
