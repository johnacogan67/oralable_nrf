#!/usr/bin/env bash
set -euo pipefail

# Unified firmware update wrapper.
# Modes:
#   --ota : BLE OTA via mcumgr
#   --swd : SWD flash + RTT/watch helpers

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

MODE=""
MODE_EXPLICIT=0

# OTA defaults
BIN_PATH=""
CONNSTRING=""

# SWD defaults
JLINK_SNR="${JLINK_SNR:-1050090445}"
WATCH_SECS="${WATCH_SECS:-30}"
WATCH_INTERVAL="${WATCH_INTERVAL:-1}"
NO_TAIL=0

usage() {
  cat <<EOF
Usage:
  $0 [--swd] [--snr <jlink_serial>] [--watch-secs <seconds>] [--watch-interval <seconds>] [--no-tail]
  $0 --ota [--bin <path>] [--connstring "<mcumgr connstring>"]

Examples:
  $0
  $0 --ota --bin "${REPO_ROOT}/build_pcb00003/app_update.bin"
  $0 --swd --snr 1050090445 --watch-secs 30 --watch-interval 1 --no-tail
EOF
}

require_file() {
  local p="$1"
  if [[ ! -f "$p" ]]; then
    echo "ERROR: required script not found: $p"
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ota)
      MODE="ota"
      MODE_EXPLICIT=1
      shift
      ;;
    --swd)
      MODE="swd"
      MODE_EXPLICIT=1
      shift
      ;;
    --bin)
      BIN_PATH="${2:-}"
      shift 2
      ;;
    --connstring)
      CONNSTRING="${2:-}"
      shift 2
      ;;
    --snr|--jlink-snr)
      JLINK_SNR="${2:-}"
      shift 2
      ;;
    --watch-secs)
      WATCH_SECS="${2:-}"
      shift 2
      ;;
    --watch-interval)
      WATCH_INTERVAL="${2:-}"
      shift 2
      ;;
    --no-tail)
      NO_TAIL=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -z "${MODE}" ]]; then
  MODE="swd"
fi

if [[ "${MODE}" == "ota" ]]; then
  OTA_SCRIPT="${REPO_ROOT}/tools/mcumgr_dfu_pcb00003.sh"
  require_file "${OTA_SCRIPT}"

  echo "==> Mode: OTA (mcumgr)"
  if [[ -n "${BIN_PATH}" && -n "${CONNSTRING}" ]]; then
    BIN_PATH="${BIN_PATH}" CONNSTRING="${CONNSTRING}" "${OTA_SCRIPT}"
  elif [[ -n "${BIN_PATH}" ]]; then
    BIN_PATH="${BIN_PATH}" "${OTA_SCRIPT}"
  elif [[ -n "${CONNSTRING}" ]]; then
    CONNSTRING="${CONNSTRING}" "${OTA_SCRIPT}"
  else
    "${OTA_SCRIPT}"
  fi
  exit 0
fi

FLASH_SCRIPT="${REPO_ROOT}/scripts/flash_and_rtt.sh"
WATCH_SCRIPT="${REPO_ROOT}/scripts/swd_watch.sh"
if [[ ! -f "${FLASH_SCRIPT}" || ! -f "${WATCH_SCRIPT}" ]]; then
  if [[ "${MODE_EXPLICIT}" -eq 0 ]]; then
    echo "SWD helper scripts are missing; falling back to OTA mode."
    echo "Missing:"
    [[ ! -f "${FLASH_SCRIPT}" ]] && echo "  - ${FLASH_SCRIPT}"
    [[ ! -f "${WATCH_SCRIPT}" ]] && echo "  - ${WATCH_SCRIPT}"
    OTA_SCRIPT="${REPO_ROOT}/tools/mcumgr_dfu_pcb00003.sh"
    require_file "${OTA_SCRIPT}"
    echo "==> Mode: OTA (mcumgr)"
    if [[ -n "${BIN_PATH}" && -n "${CONNSTRING}" ]]; then
      BIN_PATH="${BIN_PATH}" CONNSTRING="${CONNSTRING}" "${OTA_SCRIPT}"
    elif [[ -n "${BIN_PATH}" ]]; then
      BIN_PATH="${BIN_PATH}" "${OTA_SCRIPT}"
    elif [[ -n "${CONNSTRING}" ]]; then
      CONNSTRING="${CONNSTRING}" "${OTA_SCRIPT}"
    else
      "${OTA_SCRIPT}"
    fi
    exit 0
  fi
  [[ ! -f "${FLASH_SCRIPT}" ]] && echo "ERROR: required script not found: ${FLASH_SCRIPT}"
  [[ ! -f "${WATCH_SCRIPT}" ]] && echo "ERROR: required script not found: ${WATCH_SCRIPT}"
  exit 1
fi

echo "==> Mode: SWD (J-Link serial: ${JLINK_SNR})"
if [[ "${NO_TAIL}" -eq 1 ]]; then
  "${FLASH_SCRIPT}" --no-tail
else
  "${FLASH_SCRIPT}"
fi

"${WATCH_SCRIPT}" "${JLINK_SNR}" "${WATCH_SECS}" "${WATCH_INTERVAL}"
