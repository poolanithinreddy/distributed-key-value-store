# Architecture

Every node has the same responsibilities and any node can coordinate. Membership is read once from JSON configuration. A ring built from all physical node IDs and configurable virtual nodes maps a key to the first `N` distinct clockwise owners. FNV-1a supplies a portable byte hash and the MurmurHash3 64-bit finalizer removes correlated suffix patterns. Colliding vnode positions advance deterministically until unused.

Connection parsing, public handlers, and peer-internal/health handlers use separate bounded pools; replica fan-out uses a fourth pool. Reserving execution capacity for peer traffic prevents client saturation from starving the internal requests needed to complete those clients' quorums. Network calls share one absolute quorum deadline. Queues apply backpressure instead of creating unbounded threads. RAII owns sockets, WAL streams, locks, pools, health and accept threads, and node shutdown.

Each accepted record is appended and flushed to a binary WAL before the in-memory index changes. A header contains a magic value, body length, and 64-bit checksum. Startup replays valid records in order; an incomplete, over-sized, bad-magic, bad-length, or bad-checksum tail stops replay without applying that record. The maximum recovered counter seeds the node clock.

The health detector probes every remote `/health` endpoint. One failed probe is `suspect`; the configured threshold (three by default) is `unavailable`; any successful probe immediately restores `healthy`. These states are observable and advisory—quorum operations still try all selected replicas, avoiding a stale health opinion from preventing recovery.

## Request sequence

1. Validate and URL-decode the key; parse the JSON value for PUT.
2. Select `N` replicas through the stable ring.
3. For PUT/DELETE, quorum-read the current version, atomically allocate a higher counter, and fan out the value or tombstone.
4. For GET, fan out reads, require `R` valid responses (including authoritative absence), choose the deterministic newest record, and schedule repairs.
5. Return structured HTTP errors when the deadline expires or quorum is unavailable; record metrics and a value-free structured log.

## Roadmap

- Durable `fsync` policy, snapshots, compaction, and safe tombstone collection
- Hinted handoff plus periodic Merkle-tree anti-entropy
- Authenticated TLS transport, streaming/parser hardening, and per-tenant limits
- Membership epochs and automated rebalancing with data transfer
- Vector clocks or another explicit multi-value conflict model
- Multi-host benchmark and chaos environment
