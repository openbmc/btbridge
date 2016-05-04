#!/bin/bash
set -evx
make KERNEL_HEADERS=${PWD} test
LD_PRELOAD=${PWD}/bt-host.so ./btbridged --vv &
bridge_pid=$!

./ipmi-bouncer &
ipmi_pid=$!

wait $bridge_pid
exit_status=$?

kill -9 $ipmi_pid

exit $exit_status
