#!/bin/bash

# Start FE.
cd $SR_HOME/fe/bin/
# enable [FQDN access](https://docs.starrocks.io/en-us/2.4/administration/enable_fqdn#enable-fqdn-access)
echo "Start FE"
./start_fe.sh --host_type FQDN --daemon

# Start BE.
echo "Start BE"
cd $SR_HOME/be/bin/
./start_be.sh --daemon


# Start UDF HTTP server endpoint
echo "Start UDF HTTP server endpoint"
cd $SR_HOME/udf
python2 -m SimpleHTTPServer 7000 &> $SR_HOME/udf/udf.log &

# Sleep until the cluster starts.
sleep 15;

# Fetch fqdn with the command suggested by AWS official doc: https://docs.aws.amazon.com/managedservices/latest/userguide/find-FQDN.html
MYFQDN=`hostname --fqdn`
echo "Register BE ${MYFQDN} to FE"
mysql -uroot -h${MYFQDN} -P 9030 -e "alter system add backend '${MYFQDN}:9050';"

# TODO(j.yang): Explicitly set pipeline_sink_dop because pipeline load currently only
# uses parallelism 1 by default. Remove this after automatic parallelism selection
# works.
SINK_DOP=$(($(nproc) / 2))
mysql -uroot -h${MYFQDN} -P 9030 -e "set global pipeline_sink_dop=${SINK_DOP};"


# Loop to detect the process.
while sleep 60; do
  ps aux | grep starrocks_be | grep -q -v grep
  PROCESS_STATUS=$?

  if [ $PROCESS_STATUS -ne 0 ]; then
    echo "one of the starrocks process already exit."
    exit 1;
  fi
done
