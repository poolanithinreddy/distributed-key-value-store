#!/usr/bin/env bash
set -euo pipefail
base=http://127.0.0.1:8081/v1/kv/smoke
curl --silent --fail -X PUT -H 'Content-Type: application/json' -d '{"value":"one"}' "$base" | jq -e '.status == "ok"' >/dev/null
curl --silent --fail "$base" | jq -e '.value == "one"' >/dev/null
curl --silent --fail -X PUT -H 'Content-Type: application/json' -d '{"value":"two"}' "$base" | jq -e '.status == "ok"' >/dev/null
curl --silent --fail "$base" | jq -e '.value == "two"' >/dev/null
curl --silent --fail -X DELETE "$base" | jq -e '.status == "ok"' >/dev/null
status="$(curl --silent -o /dev/null -w '%{http_code}' "$base")"
[[ "$status" == 404 ]]
echo "PUT/GET/UPDATE/DELETE smoke test passed"

