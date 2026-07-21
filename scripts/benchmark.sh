#!/usr/bin/env bash
set -euo pipefail
mkdir -p benchmarks/results
for workload in put get mixed; do
  for clients in 1 8 16 32 64; do
    for trial in 1 2 3; do
      ./build/kv_benchmark 127.0.0.1 8081 "$workload" "$clients" "${WARMUP_SECONDS:-2}" \
        "${DURATION_SECONDS:-10}" "${KEYSPACE:-1000}" "${VALUE_BYTES:-128}" \
        "benchmarks/results/${workload}-c${clients}-trial${trial}.json"
    done
  done
done

