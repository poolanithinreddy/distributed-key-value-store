#!/usr/bin/env bash
set -euo pipefail
./build/kv_cli 127.0.0.1 8081 put failure-key before
pid="$(<cluster-logs/node3.pid)"
kill "$pid"
for attempt in {1..30}; do if ! kill -0 "$pid" 2>/dev/null; then break; fi; sleep 0.1; done
./build/kv_cli 127.0.0.1 8081 put failure-key during
./build/kv_cli 127.0.0.1 8082 get failure-key
./build/kv_node config/node3.json >cluster-logs/node3.log 2>&1 &
echo "$!" >cluster-logs/node3.pid
for attempt in {1..50}; do if curl --silent --fail http://127.0.0.1:8083/ready >/dev/null; then break; fi; sleep 0.1; done
./build/kv_cli 127.0.0.1 8083 get failure-key
echo "single-node failure and restart test passed"

