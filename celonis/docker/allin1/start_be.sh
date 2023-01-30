#!/bin/bash

# Start BE.
cd $SR_HOME/be/bin/

# Try stop be if there is an instance
./stop_be.sh
sleep 3
./start_be.sh --daemon

# Loop to detect the process.
while sleep 60; do
  ps aux | grep starrocks_be | grep -q -v grep
  PROCESS_STATUS=$?

  if [ PROCESS_STATUS -ne 0 ]; then
    echo "one of the starrocks process already exit."
    exit 1;
  fi
done
