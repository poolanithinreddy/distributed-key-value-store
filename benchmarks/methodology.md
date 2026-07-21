# Benchmark methodology

Run a Release build and three local node processes with `N=3/R=2/W=2`. Each trial pre-fills GET/mixed keyspaces, runs a warm-up excluded from results, then measures a fixed duration. Per-client deterministic PRNGs select keys uniformly. Mixed is 80% GET/20% PUT. Every operation opens one HTTP/1.1 connection; PUT/DELETE include the coordinator's version quorum read. Latency is end-to-end client wall time. Throughput includes successful and failed attempts; both counts are reported.

The default script matrix is three value sizes (128 B, 1 KiB, 4 KiB) × three workloads × client counts 1/8/16/32/64 × three trials. A one-node baseline uses `config/single-node.json` (`N=1/R=1/W=1`). Failure/recovery trials must record the exact kill/restart times separately from health-state recovery and data repair.

Results must state date, commit, OS, CPU, memory, compiler, build type, topology, quorum, keyspace, value size, warm-up, measured duration, and whether local, Docker, or multi-host. Raw generated JSON is intentionally ignored; reviewed summaries belong in `benchmarks/README.md`.
