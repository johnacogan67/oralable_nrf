#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-build_pcb00003}"
BOARD="${BOARD:-pcb00003/nrf52832}"
APP_DIR="${REPO_ROOT}/app"
RELEASE_CONFIG="${APP_DIR}/slot_fit.conf"
Pristine=0

usage() {
  cat <<USAGE
Usage:
  $0 [--pristine] [--build-dir <repository-relative-dir>] [--board pcb00003/nrf52832]

Canonical release source: app/
Canonical release board:  pcb00003/nrf52832
Pinned SDK:                NCS v2.7.0 from west.yml
Release size profile:      app/slot_fit.conf

After build:
  SWD first flash:  ./scripts/flash_and_rtt.sh --build-dir ${BUILD_DIR} --no-build
  OTA updates:      ./scripts/update_firmware.sh --ota
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pristine|-p) Pristine=1; shift ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    --board) BOARD="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown argument: $1"; usage; exit 1 ;;
  esac
done

if [[ "$BOARD" == "pcb00003" ]]; then
  BOARD="pcb00003/nrf52832"
fi
if [[ "$BOARD" != "pcb00003/nrf52832" ]]; then
  echo "ERROR: build_firmware.sh is Gen1-release-only; use pcb00003/nrf52832." >&2
  echo "For gated Gen2 bring-up, use scripts/build_gen2_bringup.sh with NCS v2.9.3." >&2
  exit 2
fi
if [[ "$BUILD_DIR" == /* || "$BUILD_DIR" == *".."* ]]; then
  echo "ERROR: --build-dir must be a repository-relative directory." >&2
  exit 2
fi
if [[ ! -f "$RELEASE_CONFIG" ]]; then
  echo "ERROR: required Gen1 release size profile is missing: $RELEASE_CONFIG" >&2
  exit 1
fi

if ! command -v west >/dev/null 2>&1; then
  echo "ERROR: west not found in PATH."
  exit 1
fi

python3 "${REPO_ROOT}/scripts/verify_firmware_workspace.py" --track gen1_release

echo "==> Sysbuild (${BOARD}) -> ${BUILD_DIR}"
if [[ "${Pristine}" -eq 1 ]]; then
  west build -b "${BOARD}" -d "${REPO_ROOT}/${BUILD_DIR}" -p always "${APP_DIR}" --sysbuild -- \
    -DBOARD_ROOT="${REPO_ROOT}" -DEXTRA_CONF_FILE="${RELEASE_CONFIG}"
else
  west build -b "${BOARD}" -d "${REPO_ROOT}/${BUILD_DIR}" "${APP_DIR}" --sysbuild -- \
    -DBOARD_ROOT="${REPO_ROOT}" -DEXTRA_CONF_FILE="${RELEASE_CONFIG}"
fi

BUILD_ROOT="${REPO_ROOT}/${BUILD_DIR}"
OTA_BIN="${BUILD_ROOT}/app/zephyr/zephyr.signed.bin"
DFU_ZIP="${BUILD_ROOT}/dfu_application.zip"
MERGED_HEX="${BUILD_ROOT}/merged.hex"
OTA_LINK="${BUILD_ROOT}/app_update.bin"

# Stable path for mcumgr scripts and docs.
if [[ -f "${OTA_BIN}" ]]; then
  ln -sf "app/zephyr/zephyr.signed.bin" "${OTA_LINK}"
fi

python3 "${REPO_ROOT}/scripts/verify_firmware_artifacts.py" \
  --build-dir "${BUILD_DIR}" --write-metadata

echo
echo "==> Build complete"
if [[ -f "${MERGED_HEX}" ]]; then
  echo "  SWD (first flash): ${MERGED_HEX}"
elif [[ -f "${DFU_ZIP}" ]]; then
  echo "  SWD (first flash): ${DFU_ZIP}"
else
  echo "  SWD (first flash): ${BUILD_ROOT}/app/zephyr/zephyr.hex"
fi

if [[ -f "${OTA_BIN}" ]]; then
  echo "  OTA (mcumgr):      ${OTA_BIN}"
  echo "  OTA (symlink):     ${OTA_LINK}"
else
  echo "  ERROR: OTA artifact missing: ${OTA_BIN}"
  exit 1
fi
