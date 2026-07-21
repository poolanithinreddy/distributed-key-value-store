# HTTP API

All bodies except `/metrics` are JSON. Keys are percent-encoded path segments and limited to 1,024 decoded bytes; values are JSON strings limited to 4 MiB.

| Endpoint | Success | Meaning |
|---|---:|---|
| `PUT /v1/kv/{key}` with `{"value":"..."}` | 201 | quorum committed |
| `GET /v1/kv/{key}` | 200 | latest quorum value and version |
| `DELETE /v1/kv/{key}` | 200 | tombstone quorum committed |
| `GET /health` | 200 | process handler reachable |
| `GET /ready` | 200/503 | node server started/not started |
| `GET /metrics` | 200 | Prometheus text exposition |
| `GET /v1/cluster` | 200 | static membership, N/R/W, health opinions |
| `GET /v1/stats` | 200 | compact JSON counters |

Public errors use `{"error":"quorum_unavailable","message":"..."}` and return 400 for bad input, 404 for absent/tombstoned keys, 405 for unsupported methods, 503 for quorum failure, and 500 for unexpected errors. `/internal/kv/{key}` carries versioned records between trusted peers and must not be internet-exposed; this version has no authentication.

