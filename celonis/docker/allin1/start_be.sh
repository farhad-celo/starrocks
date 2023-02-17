#!/bin/bash
log_stdin()
{
    echo "[`date`] $@" >&1
}

# Start BE.
cd $SR_HOME/be/bin/

# Try stop be if there is an instance
./stop_be.sh
sleep 3
log_stdin "start_be.sh"
./start_be.sh --daemon

# Loop to detect the heart beat.
while sleep 10; do
  BE_STATUS=`netstat -ltpn | grep 9050`
  if [ -z "$BE_STATUS" ]; then
    log_stdin "BE has exited"
    exit 1;
  fi;
  log_stdin $BE_STATUS
done
