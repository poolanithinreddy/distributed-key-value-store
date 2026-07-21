#!/usr/bin/env bash
set -euo pipefail
mkdir -p benchmarks/results
build_dir="${KV_BUILD_DIR:-build}"
for value_bytes in ${VALUE_SIZES:-128 1024 4096}; do
  for workload in put get mixed; do
    for clients in 1 8 16 32 64; do
      for trial in 1 2 3; do
        "$build_dir/kv_benchmark" 127.0.0.1 8081 "$workload" "$clients" "${WARMUP_SECONDS:-2}" \
          "${DURATION_SECONDS:-10}" "${KEYSPACE:-1000}" "$value_bytes" \
          "benchmarks/results/${workload}-c${clients}-v${value_bytes}-trial${trial}.json"
      done
    done
  done
done
