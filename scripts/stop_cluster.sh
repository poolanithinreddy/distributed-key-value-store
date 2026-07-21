#!/usr/bin/env bash
set -euo pipefail
for file in cluster-logs/node{1,2,3}.pid; do
  [[ -f "$file" ]] || continue
  pid="$(<"$file")"
  if kill -0 "$pid" 2>/dev/null; then kill "$pid"; fi
  for attempt in {1..50}; do
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.1
  done
done
echo "cluster stopped"

