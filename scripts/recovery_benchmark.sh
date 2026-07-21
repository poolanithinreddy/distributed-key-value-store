#!/usr/bin/env bash
set -euo pipefail
build_dir="${KV_BUILD_DIR:-build}"
./scripts/run_cluster.sh
cleanup() { ./scripts/stop_cluster.sh >/dev/null 2>&1 || true; }
trap cleanup EXIT

node3_pid="$(<cluster-logs/node3.pid)"
kill "$node3_pid"
for attempt in {1..100}; do
  state="$(curl -sS http://127.0.0.1:8081/v1/cluster | jq -r '.nodes[] | select(.id=="node3") | .state' 2>/dev/null || true)"
  [[ "$state" == unavailable ]] && break
  sleep 0.05
done

"$build_dir/kv_benchmark" 127.0.0.1 8081 mixed 64 "${WARMUP_SECONDS:-2}" \
  "${DURATION_SECONDS:-10}" "${KEYSPACE:-1000}" "${VALUE_BYTES:-128}" \
  benchmarks/results/mixed-c64-one-node-down.json

recovery_start="$(python3 -c 'import time; print(time.time_ns())')"
"$build_dir/kv_node" config/node3.json >cluster-logs/node3.log 2>&1 &
echo "$!" >cluster-logs/node3.pid
state="unavailable"
for attempt in {1..200}; do
  state="$(curl -sS http://127.0.0.1:8081/v1/cluster | jq -r '.nodes[] | select(.id=="node3") | .state' 2>/dev/null || true)"
  [[ "$state" == healthy ]] && break
  sleep 0.05
done
recovery_end="$(python3 -c 'import time; print(time.time_ns())')"
[[ "$state" == healthy ]]
recovery_ms="$(python3 -c "print(round(($recovery_end-$recovery_start)/1000000, 3))")"
printf '{"health_recovery_ms":%s,"definition":"restart invocation until remote detector reports healthy"}\n' \
  "$recovery_ms" >benchmarks/results/recovery.json
echo "health recovery: ${recovery_ms} ms"
