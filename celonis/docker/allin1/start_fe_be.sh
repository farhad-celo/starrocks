#!/bin/bash

log_stdin()
{
    echo "[`date`] $@" >&1
}

log_stdin "Starrting all-in-1 container..."

# Start UDF HTTP server endpoint
log_stdin "Start UDF HTTP server endpoint"
cd $SR_HOME/udf
python2 -m SimpleHTTPServer 7000 &> $SR_HOME/udf/udf.log &

# Start FE.
cd $SR_HOME/fe/bin/
# enable [FQDN access](https://docs.starrocks.io/en-us/2.4/administration/enable_fqdn#enable-fqdn-access)
log_stdin "Starting FE"
./start_fe.sh --host_type FQDN --daemon

# Start BE.
log_stdin "Starting BE"
cd $SR_HOME/be/bin/
./start_be.sh --daemon


while sleep 1; do
  FE_STATUS=$(curl -s localhost:8030/api/bootstrap | grep -o '"status":"[^"]*"' | cut -d':' -f2 | tr -d '"')
  BE_STATUS=$(curl -s localhost:8040/api/health | grep -o '"status": *"[^"]*"' | cut -d'"' -f4)
  log_stdin "FE_STATUS: $FE_STATUS, BE_STATUS: $BE_STATUS"

  if [ "$FE_STATUS" = 'OK' -a "$BE_STATUS" = 'OK' ]; then
    log_stdin "FE and BE are up"
    break
  fi

  log_stdin "wait for 1 sec for BE and FE become ready ..."
done


# Fetch fqdn with the command suggested by AWS official doc: https://docs.aws.amazon.com/managedservices/latest/userguide/find-FQDN.html
MYFQDN=`hostname --fqdn`
log_stdin "Registering BE ${MYFQDN} to FE"
mysql -uroot -h${MYFQDN} -P 9030 -e "alter system add backend '${MYFQDN}:9050';"
log_stdin "Registed BE ${MYFQDN} to FE"

mysql -uroot -h${MYFQDN} -P 9030

# Loop to detect the process.
while sleep 10; do
  if [ "$STARTMODE" = 'auto' ]; then
    PROCESS_STATUS=`mysql -uroot -h127.0.0.1 -P 9030 -e "show backends\G" |grep "Alive: true"`
    if [ -z "$PROCESS_STATUS" ]; then
      log_stdin "service has exited"
      exit 1;
    fi;
    log_stdin $PROCESS_STATUS
  elif [ "$STARTMODE" = 'debug' ]; then
    log_stdin "debug mode"
  fi
done
