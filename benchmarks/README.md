# Verified benchmark results

Measured 2026-07-21 at commit `f7452fa` using the Release build, native processes on one MacBook Air (Apple M4, 10 cores, 16 GB RAM), macOS 26.5.2 arm64, Apple Clang 21.0.0. The replicated cluster used three localhost nodes, `N=3/R=2/W=2`, 100 uniformly selected keys, 128-byte values, a one-second warm-up, two-second measured phase, and three trials per cell. Values are medians of the three trial-level results; error is the total across trials.

| Workload | Clients | ops/s | p95 | Errors |
|---|---:|---:|---:|---:|
| PUT | 1 | 1,841 | 0.560 ms | 0 |
| PUT | 8 | 5,841 | 1.518 ms | 0 |
| PUT | 16 | 5,964 | 2.764 ms | 0 |
| PUT | 32 | 5,868 | 5.429 ms | 0 |
| PUT | 64 | 5,856 | 10.844 ms | 0 |
| GET | 1 | 4,276 | 0.243 ms | 0 |
| GET | 8 | 11,497 | 0.802 ms | 0 |
| GET | 16 | 8,222 | 1.508 ms | 1 |
| GET | 32 | 11,033 | 2.830 ms | 4 |
| GET | 64 | 10,677 | 5.628 ms | 0 |
| 80% GET / 20% PUT | 1 | 3,504 | 0.483 ms | 0 |
| 80% GET / 20% PUT | 8 | 5,631 | 1.388 ms | 0 |
| 80% GET / 20% PUT | 16 | 9,178 | 2.249 ms | 0 |
| 80% GET / 20% PUT | 32 | 5,880 | 3.910 ms | 0 |
| 80% GET / 20% PUT | 64 | 8,973 | 7.414 ms | 0 |

The non-monotonic throughput and five transient GET errors demonstrate meaningful scheduler/connection variance in these deliberately short trials. Do not generalize them as steady-state capacity.

## Baseline, value size, and failure

At 64 clients and 80:20 mixed load, using the same timing and keyspace:

| Topology / value | Trials | ops/s median | p95 median | Errors |
|---|---:|---:|---:|---:|
| One node, `N=R=W=1`, 128 B | 3 | 38,901 | 1.736 ms | 0 |
| Three nodes, 128 B | 3 | 8,973 | 7.414 ms | 0 |
| Three nodes, 1 KiB | 3 | 2,961 | 24.308 ms | 0 |
| Three nodes, 4 KiB | 3 | 932 | 99.068 ms | 0 |
| Three nodes with one stopped, 128 B | 1 | 9,319 | 4.916 ms | 0 |

The failure result is one three-second measured trial and is evidence of availability, not evidence that failure improves performance. After restarting the peer, another node's detector changed it to healthy in **551.228 ms** (one trial), consistent with the configured 500 ms probe interval. A separate full-cluster restart preserved and returned a quorum-written value.

Raw JSON is generated under ignored `benchmarks/results/`. Re-run longer (at least 30 seconds per trial), on an idle host, and ideally across machines before using performance figures externally. The harness contains no expected performance constants.
