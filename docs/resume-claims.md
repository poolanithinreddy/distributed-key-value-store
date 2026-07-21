# Résumé claim evidence

## Verified by code and tests

- C++17 coordinatorless store with deterministic consistent hashing and virtual nodes.
- Configurable quorum replication (`N/R/W`), bounded concurrent fan-out, LWW versions, tombstones, and read repair.
- Thread-safe in-memory indexing, checksummed append-only WAL, restart replay, and safe corrupt-tail handling.
- Continuous three-state peer health detection, structured logs, Prometheus metrics, and bounded concurrency.
- Automated unit, three-process integration, concurrency, persistence, and failure-injection tests.

Evidence: [`tests/`](../tests), [`src/replication.cpp`](../src/replication.cpp), [`src/wal.cpp`](../src/wal.cpp), and the CI workflow.

## Benchmark claims

The 2026-07-21 local Release matrix at commit `f7452fa` did **not** reproduce 18.2k ops/s in the replicated cluster. At 64 clients with 128-byte values, median results were 5,856 PUT ops/s at 10.844 ms p95, 10,677 GET ops/s at 5.628 ms p95, and 8,973 mixed ops/s at 7.414 ms p95. Thus “14 ms p95 at 64 clients” is reproducible only when qualified by the exact short local/128-byte workloads; it is not true for the measured 1 KiB or 4 KiB workloads.

The old 2.3-second recovery number was not reproduced. Health recovery—restart until a remote detector reported healthy—was 551.228 ms in one trial. Data convergence is a different event and was not assigned a timing claim. See [the complete environment and tables](../benchmarks/README.md).

## Safe résumé bullets before measurement

- Built a C++17 coordinatorless key-value store with deterministic consistent hashing, configurable `N/R/W` quorum replication, versioned tombstones, and read repair across a local three-node cluster.
- Implemented bounded concurrent HTTP request/replica handling, continuous peer-health transitions, and explicit timeout/quorum failure responses; validated node loss and recovery with process-level and injectable-fault tests.
- Added a checksummed append-only WAL and thread-safe in-memory index, with tested restart recovery and safe handling of corrupt or truncated trailing records.
- Measured 8,973 ops/s with 7.414 ms p95 for a 64-client, 80:20 GET/PUT, 128-byte workload on a local three-node Apple M4 cluster (median of three short Release trials).

For a conservative résumé, prefer the first three bullets and keep the measured bullet for a project discussion. Remove 18.2k ops/s and 2.3-second recovery. Avoid “consensus,” “linearizable,” “production distributed database,” “network partition tolerant,” automatic rebalancing, or any number not present in reviewed evidence.
