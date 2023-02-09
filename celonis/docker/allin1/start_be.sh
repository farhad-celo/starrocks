#!/bin/bash

# Start BE.
cd $SR_HOME/be/bin/

# Try stop be if there is an instance
./stop_be.sh
sleep 3
./start_be.sh --daemon

sleep infinity
