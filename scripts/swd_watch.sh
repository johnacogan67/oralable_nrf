#!/usr/bin/env bash
set -euo pipefail

# Poll SWD connectivity for a fixed time window.
# Usage: swd_watch.sh <jlink_serial> [duration_seconds] [interval_seconds]

SNR="${1:-}"
DURATION_S="${2:-30}"
INTERVAL_S="${3:-1}"

if [[ -z "${SNR}" ]]; then
  echo "Usage: $0 <jlink_serial> [duration_seconds] [interval_seconds]"
  exit 1
fi

if ! command -v nrfjprog >/dev/null 2>&1; then
  echo "ERROR: nrfjprog not found in PATH."
  exit 1
fi

end_ts=$((SECONDS + DURATION_S))
failures=0
checks=0

echo "==> SWD watch: snr=${SNR}, duration=${DURATION_S}s, interval=${INTERVAL_S}s"

while (( SECONDS < end_ts )); do
  checks=$((checks + 1))
  timestamp="$(date '+%H:%M:%S')"
  if nrfjprog --snr "${SNR}" --memrd 0x00000000 --w 32 --n 1 >/dev/null 2>&1; then
    echo "[${timestamp}] OK"
  else
    failures=$((failures + 1))
    echo "[${timestamp}] FAIL (cannot read target over SWD)"
  fi
  sleep "${INTERVAL_S}"
done

echo "==> SWD watch done: checks=${checks}, failures=${failures}"
if (( failures > 0 )); then
  exit 1
fi
