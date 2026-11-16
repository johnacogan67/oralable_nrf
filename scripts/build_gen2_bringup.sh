#!/usr/bin/env bash
# Build the gated Gen2 BLE bring-up target. This script intentionally never flashes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NCS_ROOT=""
TOOLCHAIN=""
BUILD_DIR="build_gen2_bringup"
PRISTINE=0

usage() {
  cat <<'USAGE'
Usage: ./scripts/build_gen2_bringup.sh --ncs-root <ncs-v2.9.3-workspace> --toolchain <ncs-toolchain> [--pristine] [--build-dir <dir>]

Builds only bringup_ble/ for pcb00003_gen2/nrf54l15/cpuapp using the separately
pinned NCS v2.9.3 workspace and matching Nordic toolchain. The output is a
non-release bench artifact. This helper never invokes programming, OTA packaging,
or release-artifact promotion.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ncs-root) NCS_ROOT="${2:-}"; shift 2 ;;
    --toolchain) TOOLCHAIN="${2:-}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    --pristine|-p) PRISTINE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

[[ -n "$NCS_ROOT" ]] || { echo "ERROR: --ncs-root is required" >&2; usage; exit 1; }
[[ -n "$TOOLCHAIN" ]] || { echo "ERROR: --toolchain is required" >&2; usage; exit 1; }
[[ "$BUILD_DIR" != /* && "$BUILD_DIR" != *".."* ]] || { echo "ERROR: --build-dir must be repository-relative" >&2; exit 1; }

python3 "$SCRIPT_DIR/verify_firmware_workspace.py" \
  --track gen2_bringup \
  --ncs-root "$NCS_ROOT" \
  --toolchain "$TOOLCHAIN"

export PATH="$TOOLCHAIN/bin:$PATH"
export ZEPHYR_BASE="$NCS_ROOT/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$TOOLCHAIN/opt/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
PRISTINE_ARGS=()
[[ "$PRISTINE" -eq 1 ]] && PRISTINE_ARGS=(-p always)
(
  cd "$NCS_ROOT"
  "$TOOLCHAIN/bin/west" build \
    -b pcb00003_gen2/nrf54l15/cpuapp \
    -s "$ROOT/bringup_ble" \
    -d "$ROOT/$BUILD_DIR" \
    "${PRISTINE_ARGS[@]}" \
    -- -DBOARD_ROOT="$ROOT"
)
HEX="$ROOT/$BUILD_DIR/zephyr/zephyr.hex"
ELF="$ROOT/$BUILD_DIR/zephyr/zephyr.elf"
test -s "$HEX" && test -s "$ELF"
echo "Gen2 bring-up build complete: $HEX"
echo "NON-RELEASE: no programming, OTA packaging, artifact promotion, or claim promotion was performed."
