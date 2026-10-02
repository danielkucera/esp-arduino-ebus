#!/usr/bin/env bash
# Dumps all relevant read-only API endpoints into one timestamped JSON file
# for attaching to a debug session. Never calls POST/action endpoints.
#
# Usage: ./dump_endpoints.sh [host] [output-file]
#   host defaults to esp-ebus.local, output defaults to
#   ebus_dump_YYYYMMDD_HHMMSS.json in the current directory.
# Run twice some minutes apart; diffing the two snapshots shows deltas
# (error_active growth, heap drift, quarantines, ...).
set -u

HOST="${1:-esp-ebus.local}"
OUT="${2:-ebus_dump_$(date +%Y%m%d_%H%M%S).json}"

python3 - "$HOST" "$OUT" <<'EOF'
import json
import sys
import time
import urllib.request

host, out = sys.argv[1], sys.argv[2]

ENDPOINTS = [
    "api/v1/health",
    "api/v1/system",
    "api/v1/system/heap",
    "api/v1/system/tasks",
    "api/v1/network",
    "api/v1/devices",
    "api/v1/metrics",
    "api/v1/app/values",
    "api/v1/app/logs",
    "api/v1/app/tap",
    "api/v1/app/config",
]

result = {
    "_meta": {
        "host": host,
        "timestamp_ms": int(time.time() * 1000),
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
}

for ep in ENDPOINTS:
    url = f"http://{host}/{ep}"
    try:
        with urllib.request.urlopen(url, timeout=15) as r:
            body = r.read().decode("utf-8", errors="replace")
        try:
            result[ep] = json.loads(body)
        except json.JSONDecodeError:
            result[ep] = {"_raw": body[:2000], "_note": "not JSON"}
    except Exception as e:  # noqa: BLE001 - report per-endpoint, keep going
        result[ep] = {"_error": f"{type(e).__name__}: {e}"}

with open(out, "w") as f:
    json.dump(result, f, indent=1)
    f.write("\n")

ok = sum(1 for ep in ENDPOINTS if "_error" not in result[ep])
print(f"wrote {out}: {ok}/{len(ENDPOINTS)} endpoints OK")
sys.exit(0 if ok == len(ENDPOINTS) else 1)
EOF
