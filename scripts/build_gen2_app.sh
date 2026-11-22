#!/usr/bin/env bash
# Build the gated Gen2 patient-app candidate. This script intentionally never flashes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NCS_ROOT=""
TOOLCHAIN=""
BUILD_DIR="build_gen2_candidate"
PRISTINE=0

usage() {
  cat <<'USAGE'
Usage: ./scripts/build_gen2_app.sh --ncs-root <ncs-v2.9.3-workspace> --toolchain <ncs-toolchain> [--pristine] [--build-dir <dir>]

Builds app/ for pcb00003_gen2/nrf54l15/cpuapp using the separately pinned NCS
v2.9.3 workspace and matching Nordic toolchain. Output is a non-release Gen2
candidate used only for evidence collection. This helper never flashes, packages
an OTA, copies into artifacts/, or promotes a release claim.
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
  --track gen2_patient_candidate \
  --ncs-root "$NCS_ROOT" \
  --toolchain "$TOOLCHAIN"

python3 "$SCRIPT_DIR/require_image_version_bump.py" check gen2

export PATH="$TOOLCHAIN/bin:$PATH"
export ZEPHYR_BASE="$NCS_ROOT/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$TOOLCHAIN/opt/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
PRISTINE_ARGS=()
[[ "$PRISTINE" -eq 1 ]] && PRISTINE_ARGS=(-p always)

(
  cd "$NCS_ROOT"
  "$TOOLCHAIN/bin/west" build --sysbuild \
    -b pcb00003_gen2/nrf54l15/cpuapp \
    -s "$ROOT/app" \
    -d "$ROOT/$BUILD_DIR" \
    ${PRISTINE_ARGS[@]+"${PRISTINE_ARGS[@]}"} \
    -- -DBOARD_ROOT="$ROOT"
)

MERGED="$ROOT/$BUILD_DIR/merged.hex"
SIGNED="$ROOT/$BUILD_DIR/app/zephyr/zephyr.signed.bin"
ELF="$ROOT/$BUILD_DIR/app/zephyr/zephyr.elf"
test -s "$MERGED" && test -s "$SIGNED" && test -s "$ELF"
python3 "$SCRIPT_DIR/require_image_version_bump.py" record gen2

echo "Gen2 patient candidate build complete: $MERGED"
echo "NON-RELEASE: no flashing, OTA packaging, artifact promotion, or compatibility claim was performed."
echo "Next: record planned physical evidence through docs/GEN2_PROGRAM_GATE.md."
