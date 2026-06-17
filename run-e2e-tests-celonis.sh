#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
cd "$SCRIPT_DIR"

# ---------------------------------------------------------
# Usage function
# ---------------------------------------------------------
usage() {
    echo "Usage: $0 [-d directory] [-f file_filter]"
    echo "  -d  Target test directory (Default: sql/celonis if neither -d nor -f is specified)"
    echo "  -f  Target file filter"
    echo "Note: -d and -f are mutually exclusive."
    exit 1
}

TEST_DIR=""
FILE_FILTER=""

# Parse command line options
while getopts "d:f:h" opt; do
    case ${opt} in
        d ) TEST_DIR="$OPTARG" ;;
        f ) FILE_FILTER="$OPTARG" ;;
        h ) usage ;;
        * ) usage ;;
    esac
done

if [ -n "$TEST_DIR" ] && [ -n "$FILE_FILTER" ]; then
    echo "Error: TEST_DIR (-d) and FILE_FILTER (-f) cannot be used together."
    usage
fi

# Set default if neither is provided
if [ -z "$TEST_DIR" ] && [ -z "$FILE_FILTER" ]; then
    TEST_DIR="sql/celonis"
fi

echo "======================================================"
if [ -n "$FILE_FILTER" ]; then
    echo "Target File Filter    : $FILE_FILTER"
else
    echo "Target Test Directory : $TEST_DIR"
fi
echo "======================================================"

set -e

CONF_FILE="./test/conf/sr.conf"
BACKUP_FILE="${CONF_FILE}.bak"

echo "Backing up and modifying ${CONF_FILE}..."
cp "$CONF_FILE" "$BACKUP_FILE"

trap 'echo "Restoring ${CONF_FILE} to initial state..."; mv "$BACKUP_FILE" "$CONF_FILE"' EXIT

sed '/^\[cluster\]/,/^\[/ {
    s/^ *host *=.*$/  host = 127.0.0.1/
    s/^ *port *=.*$/  port = 9030/
}' "$BACKUP_FILE" > "$CONF_FILE"

echo "Executing tests..."
cd test

if [ -n "$FILE_FILTER" ]; then
    python run.py --file_filter="$FILE_FILTER"
else
    python run.py --dir "$TEST_DIR"
fi

cd ..

echo "Tests completed successfully."

