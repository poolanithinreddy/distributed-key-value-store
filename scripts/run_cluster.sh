#!/usr/bin/env bash
set -euo pipefail
mkdir -p cluster-logs data
build_dir="${KV_BUILD_DIR:-build}"
for number in 1 2 3; do
  if [[ -f "cluster-logs/node${number}.pid" ]] && kill -0 "$(<"cluster-logs/node${number}.pid")" 2>/dev/null; then
    echo "node${number} is already running"
    continue
  fi
  "$build_dir/kv_node" "config/node${number}.json" >"cluster-logs/node${number}.log" 2>&1 &
  echo "$!" >"cluster-logs/node${number}.pid"
done
for port in 8081 8082 8083; do
  for attempt in {1..50}; do
    if curl --silent --fail "http://127.0.0.1:${port}/ready" >/dev/null; then break; fi
    sleep 0.1
  done
  curl --silent --fail "http://127.0.0.1:${port}/ready" >/dev/null
done
echo "three-node cluster ready on ports 8081-8083"
