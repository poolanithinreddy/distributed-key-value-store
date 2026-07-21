# Known limitations

- Static membership only; no gossip, membership epochs, or automatic data transfer/rebalancing.
- Deterministic LWW can discard a concurrent value; no sibling values or vector clocks.
- Read repair only; no hinted handoff or periodic anti-entropy, so convergence requires reads.
- Tombstones never expire, preventing resurrection but growing storage.
- WAL flushes userspace buffers but does not `fsync`, compact, snapshot, or reclaim disk.
- No TLS, peer/client authentication, authorization, encryption at rest, quotas, or audit sink.
- HTTP parser is deliberately bounded and minimal; no chunked bodies, keep-alive, HTTP/2, or proxy trust model.
- Metrics are process-local and latency uses sum/max rather than histogram buckets.
- Benchmarks here run all nodes and clients on one laptop; they do not represent a multi-host production deployment.

