# Consistency model

The default configuration is `N=3`, `R=2`, `W=2`. Since `R + W > N`, completed reads and writes overlap at least one replica under a stable membership view. This is quorum replication, not a consensus protocol, and does not provide linearizability, serializable transactions, compare-and-swap, or multi-key atomicity.

Versions are `(logical counter, origin node ID)`. Before PUT or DELETE, a coordinator reads a quorum and advances its clock beyond the newest observed counter. Concurrent writes that observe the same predecessor can still have equal counters; lexicographically greater origin ID wins. If metadata ties exactly, tombstones beat values and then value bytes break the tie. This is deterministic last-write-wins, not vector-clock causality.

A GET waits for `R` transport-valid responses. Missing keys count as valid absence. It selects the newest returned record; a tombstone yields 404. Responding stale/missing replicas receive best-effort repair. A timeout before `R` responses yields 503 rather than guessing. Writes similarly return success only at `W`; replicas updated before a failed quorum are not rolled back.

Failed nodes may miss writes. They converge when a read includes them and a newer replica, but there is no guaranteed background anti-entropy. Static membership must be identical across nodes.

