#!/bin/bash

# Start FE.
cd $SR_HOME/fe/bin/

# Try stop fe first if there is a running instance
./stop_fe.sh
sleep 3

# enable [FQDN access](https://docs.starrocks.io/en-us/2.4/administration/enable_fqdn#enable-fqdn-access)
./start_fe.sh --host_type FQDN --daemon

sleep 30;

MYFQDN=`hostname --fqdn`
mysql -uroot -h${MYFQDN} -P 9030

