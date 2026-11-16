#!/usr/bin/env bash
# Restore the pinned Gen1 NCS dependency workspace. This script never builds or flashes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'USAGE'
Usage: ./scripts/bootstrap_gen1_deps.sh

Run from a clean oralable_nrf checkout. Initializes the repository as a west
workspace (if needed) and restores the NCS revisions pinned by west.yml.
Network access is required for west update. This script does not build or flash.
USAGE
  exit 0
fi

if ! command -v west >/dev/null 2>&1; then
  echo "ERROR: west is required. Install the Nordic/NCS west environment first." >&2
  exit 1
fi

cd "$ROOT"
if [[ ! -d .west ]]; then
  west init -l .
fi
if [[ "$(west topdir)" != "$ROOT" ]]; then
  echo "ERROR: this checkout is associated with a different west workspace: $(west topdir)" >&2
  exit 1
fi

west update
python3 scripts/verify_firmware_workspace.py --track gen1_release
echo "Gen1 NCS dependencies are restored. Next (no device action): ./scripts/build_firmware.sh --pristine"
