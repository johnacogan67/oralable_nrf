#!/usr/bin/env bash
# Gen1 / Gen2 track status for oralable_nrf. It never builds, flashes, or updates hardware.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "=== Oralable firmware tracks ==="
echo "Repository: $ROOT"
echo "Branch:     $(git branch --show-current 2>/dev/null || echo '?')"
echo "HEAD:       $(git log -1 --oneline 2>/dev/null || echo '?')"
echo

echo "--- Gen1 release candidate (pcb00003/nrf52832) ---"
if [[ -d boards/byteexplain/pcb00003 ]]; then
  echo "Board tree: OK"
else
  echo "Board tree: MISSING"
fi
if [[ -f app/VERSION ]]; then
  echo -n "app/VERSION: "
  tr '\n' ' ' < app/VERSION | sed 's/ *= */=/g'
  echo
fi
echo "Gen1 build: ./scripts/build_firmware.sh --pristine"
echo

echo "--- Gen2 evidence-gated program (pcb00003_gen2/nrf54l15/cpuapp) ---"
if [[ -d boards/byteexplain/pcb00003_gen2 ]]; then
  echo "Board definition: present (non-release)"
else
  echo "Board definition: MISSING"
fi
if [[ -f boards/byteexplain/pcb00003_gen2/VERSION.gen2 ]]; then
  echo -n "VERSION.gen2: "
  tr '\n' ' ' < boards/byteexplain/pcb00003_gen2/VERSION.gen2 | sed 's/ *= */=/g'
  echo
fi
python3 scripts/verify_gen2_program.py
echo "Bench candidate:   ./scripts/build_gen2_bringup.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5 --pristine"
echo "Patient candidate: ./scripts/build_gen2_app.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5 --pristine"
echo "Release status: CLOSED until reviewed G2-P0 through G2-P6 evidence exists."
echo

echo "--- Branches (local) ---"
git branch --list 'feature/gen2*' 'known-good*' 'main' 2>/dev/null || true
echo
echo "Program gate: docs/GEN2_PROGRAM_GATE.md"
echo "Cross-repo tracker: ../cursor_oralable/docs/GEN1_GEN2_TRACKING.md"
echo "Done."
