# Testing

`ctest --test-dir build --output-on-failure` runs three categories:

- Unit: stable known hash, distribution, remapping, unique replicas, version/tombstone conflicts, concurrent storage, WAL replay/corrupt tails, failure transitions, metrics, and invalid quorum configuration.
- Integration: three real node processes and sockets; create/read/update/delete, expected replication, one-node availability, two-node quorum loss, restart/persistence, stale read repair, 16 concurrent clients, and shutdown/port reuse.
- Failure injection: delayed/erroring/dropped replicas, partial write, quorum loss, stale value repair, and tombstone propagation through a controlled replica client.

CI builds Debug and Release, runs all categories, checks formatting, runs clang-tidy where available, and executes ASan/UBSan. TSan is a separate local build because it cannot be combined with ASan and is unreliable on some hosted macOS/container environments.

Local validation on 2026-07-21 passed Debug, clean Release, ASan/UBSan, and standalone TSan suites. Apple ASan reported leak detection unsupported, so memory/UB checks ran without LeakSanitizer. `clang-format` passed; `clang-tidy` was unavailable locally and remains enforced in CI. Docker Compose configuration parsing passed; the image build was not needed for the native validation.
