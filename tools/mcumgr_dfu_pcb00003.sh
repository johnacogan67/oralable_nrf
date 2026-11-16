#!/usr/bin/env bash
set -euo pipefail

# OTA DFU helper for pcb00003 using mcumgr over BLE.
#
# Defaults:
# - artifact: build_pcb00003/app_update.bin
# - BLE connstring: peer_name=Oralable
#
# Override via env:
#   BIN_PATH=/abs/path/to/app_update.bin
#   CONNSTRING="peer_name=Oralable"
#   CONNTYPE="ble"
#   RETRIES=5
#   RETRY_DELAY_S=2

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BIN_PATH="${BIN_PATH:-${REPO_ROOT}/build_pcb00003/app_update.bin}"
CONNTYPE="${CONNTYPE:-ble}"
CONNSTRING="${CONNSTRING:-peer_name=Oralable}"
RETRIES="${RETRIES:-5}"
RETRY_DELAY_S="${RETRY_DELAY_S:-2}"

MCUMGR_BASE=(mcumgr --conntype "${CONNTYPE}" --connstring "${CONNSTRING}")

usage() {
  cat <<EOF
Usage:
  $0 [bin_path]

Examples:
  $0
  $0 /Users/johnacogan67/work/oralable_nrf/build_pcb00003/app_update.bin
  CONNSTRING="peer_name=Oralable" $0
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ -n "${1:-}" ]]; then
  BIN_PATH="$1"
fi

if ! command -v mcumgr >/dev/null 2>&1; then
  echo "ERROR: mcumgr not found in PATH."
  echo "Install: pipx install mcumgr (or your preferred method)."
  exit 1
fi

if [[ ! -f "${BIN_PATH}" ]]; then
  echo "ERROR: Firmware artifact not found: ${BIN_PATH}"
  exit 1
fi

run_mcumgr() {
  local attempt=1
  while true; do
    if "${MCUMGR_BASE[@]}" "$@"; then
      return 0
    fi
    if (( attempt >= RETRIES )); then
      echo "ERROR: mcumgr command failed after ${RETRIES} attempts: $*"
      return 1
    fi
    echo "Retry ${attempt}/${RETRIES} failed for: $*"
    sleep "${RETRY_DELAY_S}"
    ((attempt++))
  done
}

get_slot1_hash() {
  run_mcumgr image list | awk '
    /slot=1/ {in_slot1=1; next}
    in_slot1 && /hash:/ {print $2; exit}
  '
}

echo "==> Using artifact: ${BIN_PATH}"
echo "==> Transport: ${CONNTYPE} | connstring: ${CONNSTRING}"

echo
echo "==> Current image list"
run_mcumgr image list

echo
echo "==> Uploading image"
run_mcumgr image upload -e "${BIN_PATH}"

echo
echo "==> Image list after upload"
run_mcumgr image list

NEW_HASH="$(get_slot1_hash || true)"
if [[ -z "${NEW_HASH}" ]]; then
  echo "ERROR: Could not determine uploaded slot=1 image hash."
  exit 1
fi
echo "==> Uploaded image hash (slot=1): ${NEW_HASH}"

echo
echo "==> Marking uploaded image as test"
run_mcumgr image test "${NEW_HASH}"

echo
echo "==> Resetting device to boot test image"
run_mcumgr reset

echo "==> Waiting for reboot..."
sleep 8

echo
echo "==> Confirming running image"
run_mcumgr image confirm

echo
echo "==> Final image list"
run_mcumgr image list

echo
echo "DFU complete."
