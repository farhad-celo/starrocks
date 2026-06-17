#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
cd "$SCRIPT_DIR"

echo "======================================================"
echo "Starting StarRocks Cluster..."
echo "======================================================"

echo "Stopping any existing daemons..."
./output/be/bin/stop_be.sh || true
./output/fe/bin/stop_fe.sh || true

sleep 2

echo "Starting daemons..."
./output/be/bin/start_be.sh --daemon
./output/fe/bin/start_fe.sh --daemon

set +e

echo "Waiting for Backend log to populate..."
IP=""
while [ -z "$IP" ]; do
    sleep 2
    IP=$(grep "backend_options.*localhost" "$SCRIPT_DIR/output/be/log/be.INFO" 2>/dev/null | tail -1 | awk '{ print $6 }')
done
echo "Backend IP found: $IP"

echo "Waiting for Frontend to accept MySQL connections..."
until mysql -h 127.0.0.1 -P9030 -uroot -e "SELECT 1" &> /dev/null; do
    sleep 2
done

set -e

echo "Checking if backend ${IP}:9050 already exists..."
if mysql -h 127.0.0.1 -P9030 -uroot -N -e "SHOW BACKENDS;" | grep -q "$IP"; then
    echo "Backend ${IP}:9050 already exists. Skipping addition."
else
    echo "Backend not found. Adding backend..."
    mysql -h 127.0.0.1 -P9030 -uroot -e "alter system add backend '${IP}:9050';"
fi

echo "Waiting 5 seconds for cluster stabilization..."
sleep 5

echo "Cluster is up and connected!"
