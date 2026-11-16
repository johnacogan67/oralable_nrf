#!/usr/bin/env bash
# Create the separately pinned NCS workspace required for Gen2 bring-up only.
set -euo pipefail

NCS_ROOT=""
usage() {
  cat <<'USAGE'
Usage: ./scripts/bootstrap_gen2_ncs.sh --ncs-root <empty-directory>

Initializes an NCS v2.9.3 west workspace from Nordic's sdk-nrf manifest and
restores its pinned dependencies. This is for the gated Gen2 bring-up target
only; it does not build, flash, generate release artifacts, or modify Gen1.
Network access is required.
USAGE
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ncs-root) NCS_ROOT="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage; exit 1 ;;
  esac
done
[[ -n "$NCS_ROOT" ]] || { echo "ERROR: --ncs-root is required" >&2; usage; exit 1; }

if ! command -v west >/dev/null 2>&1; then
  echo "ERROR: west is required. Install the Nordic/NCS west environment first." >&2
  exit 1
fi
if [[ -e "$NCS_ROOT" && -n "$(find "$NCS_ROOT" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "ERROR: --ncs-root must not contain an existing workspace: $NCS_ROOT" >&2
  exit 1
fi

west init -m https://github.com/nrfconnect/sdk-nrf --mr v2.9.3 "$NCS_ROOT"
(
  cd "$NCS_ROOT"
  west update
)
EXPECTED="09eb26eab8d23b1589bd44ac2a896276bd3ff353"
ACTUAL="$(git -C "$NCS_ROOT/nrf" rev-parse HEAD)"
[[ "$ACTUAL" == "$EXPECTED" ]] || {
  echo "ERROR: NCS sdk-nrf did not resolve to the expected v2.9.3 commit" >&2
  exit 1
}
echo "Gen2 NCS v2.9.3 dependency workspace restored at: $NCS_ROOT"
echo "Supply the matching Nordic toolchain directory (b8efef2ad5) explicitly for every candidate build."
echo "Patient candidate (non-release): ./scripts/build_gen2_app.sh --ncs-root $NCS_ROOT --toolchain /path/to/b8efef2ad5 --pristine"
echo "Bench advertiser (non-release): ./scripts/build_gen2_bringup.sh --ncs-root $NCS_ROOT --toolchain /path/to/b8efef2ad5 --pristine"
