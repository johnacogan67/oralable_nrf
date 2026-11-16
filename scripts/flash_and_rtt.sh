#!/usr/bin/env bash
set -euo pipefail

# Flash firmware over SWD and optionally stream RTT logs.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SNR="${JLINK_SNR:-1050090445}"
HEX_PATH=""
LOG_PATH="${REPO_ROOT}/artifacts/rtt.log"
TAIL_LOG=1
BUILD_FIRST=1
BUILD_DIR="${BUILD_DIR:-build_pcb00003}"
BOARD="${BOARD:-pcb00003/nrf52832}"
APP_DIR="${APP_DIR:-app}"

usage() {
  cat <<EOF
Usage:
  $0 [--build|--no-build] [--build-dir <dir>] [--board pcb00003/nrf52832] [--app-dir app]
     [--snr <jlink_serial>] [--hex <firmware.hex>] [--log <rtt_log_path>] [--tail|--no-tail]

Defaults:
  --snr ${SNR}
  --build enabled by default
  --build-dir ${BUILD_DIR}
  --board ${BOARD}
  --app-dir ${APP_DIR}
  --hex auto-detected from common build outputs
  --log ${LOG_PATH}
  --tail enabled
EOF
}

auto_pick_hex() {
  local candidates=(
    "${REPO_ROOT}/${BUILD_DIR}/merged.hex"
    "${REPO_ROOT}/${BUILD_DIR}/dfu_application.zip"
    "${REPO_ROOT}/${BUILD_DIR}/app/zephyr/zephyr.signed.hex"
    "${REPO_ROOT}/${BUILD_DIR}/app/zephyr/zephyr.hex"
  )
  local p
  for p in "${candidates[@]}"; do
    if [[ -f "${p}" ]]; then
      echo "${p}"
      return 0
    fi
  done
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --snr)
      SNR="${2:-}"
      shift 2
      ;;
    --build)
      BUILD_FIRST=1
      shift
      ;;
    --no-build)
      BUILD_FIRST=0
      shift
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
      ;;
    --board)
      BOARD="${2:-}"
      shift 2
      ;;
    --app-dir)
      APP_DIR="${2:-}"
      shift 2
      ;;
    --hex)
      HEX_PATH="${2:-}"
      shift 2
      ;;
    --log)
      LOG_PATH="${2:-}"
      shift 2
      ;;
    --tail)
      TAIL_LOG=1
      shift
      ;;
    --no-tail)
      TAIL_LOG=0
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

if [[ "$BOARD" == "pcb00003" ]]; then
  BOARD="pcb00003/nrf52832"
fi
if [[ "$BOARD" != "pcb00003/nrf52832" || "$APP_DIR" != "app" ]]; then
  echo "ERROR: flash_and_rtt.sh supports only the canonical Gen1 release app/ on pcb00003/nrf52832." >&2
  echo "Gen2 is gated and must not be flashed through this helper." >&2
  exit 2
fi
if [[ "$BUILD_DIR" == /* || "$BUILD_DIR" == *".."* ]]; then
  echo "ERROR: --build-dir must be repository-relative." >&2
  exit 2
fi

if [[ "${BUILD_FIRST}" -eq 1 ]]; then
  BUILD_SCRIPT="${REPO_ROOT}/scripts/build_firmware.sh"
  if [[ ! -f "${BUILD_SCRIPT}" ]]; then
    echo "ERROR: build script not found: ${BUILD_SCRIPT}"
    exit 1
  fi
  echo "==> Building MCUboot + signed app (${BOARD}) in ${BUILD_DIR}"
  BUILD_DIR="${BUILD_DIR}" BOARD="${BOARD}" "${BUILD_SCRIPT}"
  if [[ -z "${HEX_PATH}" ]]; then
    if [[ -f "${REPO_ROOT}/${BUILD_DIR}/merged.hex" ]]; then
      HEX_PATH="${REPO_ROOT}/${BUILD_DIR}/merged.hex"
    elif [[ -f "${REPO_ROOT}/${BUILD_DIR}/dfu_application.zip" ]]; then
      HEX_PATH="${REPO_ROOT}/${BUILD_DIR}/dfu_application.zip"
    else
      HEX_PATH="${REPO_ROOT}/${BUILD_DIR}/app/zephyr/zephyr.hex"
    fi
  fi
fi

if ! command -v nrfjprog >/dev/null 2>&1; then
  echo "ERROR: nrfjprog not found in PATH."
  exit 1
fi

if [[ -z "${HEX_PATH}" ]]; then
  if ! HEX_PATH="$(auto_pick_hex)"; then
    echo "ERROR: Could not auto-detect firmware hex."
    echo "Pass one explicitly, e.g.:"
    echo "  $0 --hex \"${REPO_ROOT}/${BUILD_DIR}/merged.hex\""
    exit 1
  fi
fi

if [[ ! -f "${HEX_PATH}" ]]; then
  echo "ERROR: hex file not found: ${HEX_PATH}"
  exit 1
fi

echo "==> Flashing ${HEX_PATH}"
echo "==> J-Link serial: ${SNR}"
nrfjprog --snr "${SNR}" --program "${HEX_PATH}" --sectorerase --verify --reset
echo "==> Flash complete"

if [[ "${TAIL_LOG}" -eq 0 ]]; then
  exit 0
fi

if ! command -v JLinkRTTLogger >/dev/null 2>&1; then
  echo "WARNING: JLinkRTTLogger not found; skipping RTT tail."
  exit 0
fi

mkdir -p "$(dirname "${LOG_PATH}")"

RTT_ADDR=""
ELF_CANDIDATES=(
  "${REPO_ROOT}/${BUILD_DIR}/app/zephyr/zephyr.elf"
  "${REPO_ROOT}/${BUILD_DIR}/zephyr/zephyr.elf"
)
for elf in "${ELF_CANDIDATES[@]}"; do
  if [[ -f "${elf}" ]] && command -v nm >/dev/null 2>&1; then
    # awk exits on the first hit, so nm gets SIGPIPE. With pipefail that is
    # status 141 and set -e would abort before RTT attach.
    RTT_ADDR="$(nm "${elf}" 2>/dev/null | awk '/ _SEGGER_RTT$/{print "0x"$1; exit}')" || true
    if [[ -n "${RTT_ADDR}" ]]; then
      break
    fi
  fi
done

echo "==> Waiting for target boot before RTT attach"
sleep 3

echo "==> Capturing RTT to ${LOG_PATH}"
if [[ -n "${RTT_ADDR}" ]]; then
  echo "==> RTT control block address: ${RTT_ADDR}"
fi

RTT_ARGS=(
  -Device nRF52832_xxAA
  -If SWD
  -Speed 4000
  -USB "${SNR}"
  -RTTChannel 0
)
if [[ -n "${RTT_ADDR}" ]]; then
  RTT_ARGS+=(-RTTAddress "${RTT_ADDR}")
fi
RTT_ARGS+=("${LOG_PATH}")

LOGGER_PID=""
cleanup() {
  if [[ -n "${LOGGER_PID}" ]]; then
    kill "${LOGGER_PID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT INT TERM

for attempt in 1 2 3 4 5; do
  rm -f "${LOG_PATH}"
  JLinkRTTLogger "${RTT_ARGS[@]}" >/tmp/jlink_rttlogger.out 2>&1 &
  LOGGER_PID=$!

  for _ in $(seq 1 20); do
    if [[ -s "${LOG_PATH}" ]]; then
      break 2
    fi
    if ! kill -0 "${LOGGER_PID}" >/dev/null 2>&1; then
      break
    fi
    sleep 0.5
  done

  kill "${LOGGER_PID}" >/dev/null 2>&1 || true
  wait "${LOGGER_PID}" >/dev/null 2>&1 || true
  LOGGER_PID=""
  echo "==> RTT attach attempt ${attempt} failed; retrying..."
  sleep 2
done

if [[ ! -s "${LOG_PATH}" ]]; then
  echo "WARNING: RTT log not created. See /tmp/jlink_rttlogger.out"
  tail -20 /tmp/jlink_rttlogger.out 2>/dev/null || true
  exit 0
fi

tail -f "${LOG_PATH}"
