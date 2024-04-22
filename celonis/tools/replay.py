"""Replays queries in a csv file of recorded audit logs exported from DataDog.

It sends non-data-manipulation queries in order with intervals based on the original query start times and with the
original resource groups.

Usage: python replay.py <filename> <query_port_number>

query_port_number: The local port number forwarded to celostar-fe-0:9030.

How to export queries from DataDog
1. Search for "@AuditEvent.IsQuery:true" and a specific cluster, for example, "@AuditEvent.IsQuery:true cluster:us-2-cs-3b0dfab".
2. Limit the time range to replay.
3. Select "Options" and add the following columns.
  - @AuditEvent.QueryId
  - @AuditEvent.ResourceGroup
4. Select "Download as CSV".

This is an example link with search and columns.
https://celonis.datadoghq.com/logs?query=%40AuditEvent.IsQuery%3Atrue%20cluster%3Aus-2-cs-3b0dfab%20&cols=%40AuditEvent.QueryId%2C%40AuditEvent.ResourceGroup%2C%40AuditEvent.Time%2C%40AuditEvent.State&fromUser=true&index=%2A&messageDisplay=inline&refresh_mode=paused&storage=hot&stream_sort=time%2Cdesc&viz=stream&from_ts=1713455100000&to_ts=1713455580000&live=false
"""

import csv
import datetime
import subprocess
import sys
import time

def is_data_manipulation_stmt(stmt):
    # List of keywords that indicate data manipulation statements
    data_manipulation_keywords = ['insert', 'update', 'delete', 'create', 'drop', 'alter', 'truncate']
    stmt = stmt.lower()
    for keyword in data_manipulation_keywords:
        if keyword in stmt:
            return True
    return False

def parse_csv(file_path):
    sql_statements = []
    seen_query_ids = set()
    with open(file_path, 'r') as file:
        reader = csv.DictReader(file)
        for row in reader:
            message = row['Message']
            fields = message.split('|')
            timestamp = None
            stmt = None
            resource_group = None
            query_id = None
            for field in fields:
                if field.startswith('Timestamp='):
                    timestamp = int(field.split('=')[1]) / 1000.
                elif field.startswith('Stmt='):
                    stmt_field = '='.join(field.split('=')[1:])
                    stmt = stmt_field
                elif field.startswith('ResourceGroup='):
                    resource_group = field.split('=')[1]
                elif field.startswith('QueryId='):
                    query_id = field.split('=')[1]
            if timestamp is not None and stmt is not None and query_id is not None:
                if query_id not in seen_query_ids:
                    seen_query_ids.add(query_id)
                    if not is_data_manipulation_stmt(stmt):
                        modified_stmt = "set query_timeout = 150; set enable_profile=true;"
                        if resource_group and resource_group != "default_wg":
                            modified_stmt += f" set resource_group='{resource_group}';"
                        modified_stmt += f" {stmt}"
                        sql_statements.append((timestamp, modified_stmt))
                    else:
                        print(f"Skipping data manipulation statement: {stmt}")
                else:
                    print(f"Skipping duplicate QueryId: {query_id}")
    return sorted(sql_statements, key=lambda x: x[0])


def replay_sql(sql_statements, start_time, port_number):
    first_timestamp = sql_statements[0][0]

    for timestamp, stmt in sql_statements:
        delay = (timestamp - first_timestamp) - (time.time() - start_time)
        if delay > 0:
            time.sleep(delay)

        print(f"{datetime.datetime.fromtimestamp(timestamp)} {stmt[:120]}")
        # Replace single quotes with '\'', then execute SQL statement
        quoted_stmt = stmt.replace("'", r"'\''")
        mysql_cmd = f"echo '{quoted_stmt}' | mysql -A -h 127.0.0.1 -P {port_number} -u root > /dev/null 2>&1 &"
        subprocess.run(mysql_cmd, shell=True)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python replay.py <filename> <port_number>")
        sys.exit(1)

    file_path = sys.argv[1]
    port_number = int(sys.argv[2])
    sql_statements = parse_csv(file_path)
    start_time = time.time()
    replay_sql(sql_statements, start_time, port_number)
