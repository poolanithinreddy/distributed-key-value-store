# Failure model

The implementation handles crash-stop peers, refused connections, bounded network delay, explicit replica errors, truncated/corrupt WAL tails, and recovery after reachability returns. It does not model Byzantine nodes or guarantee correctness under inconsistent membership files.

Connect, send, and receive calls are bounded by the request timeout. The test client can inject delay, drops, errors, stale values, and partial writes without Docker networking. The process integration test actually terminates and restarts nodes, validates quorum survival/loss, and validates WAL recovery.

“Health recovery time” means elapsed wall-clock time from a restarted peer accepting `/health` until another node's periodic detector next records it healthy. Its upper bound is approximately one health interval plus one probe duration and scheduling delay. It is distinct from data convergence: read repair is triggered only by a qualifying read.

A one-way dropped request is failure injection, not claimed as a bidirectional network partition. There is no split-brain prevention through leadership or leases.

