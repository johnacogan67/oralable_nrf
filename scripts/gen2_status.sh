#!/usr/bin/env bash
# Gen1 / Gen2 tracking status for oralable_nrf
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "=== Oralable FW tracks ==="
echo "Repo: $ROOT"
echo "Branch: $(git branch --show-current 2>/dev/null || echo '?')"
echo "HEAD:   $(git log -1 --oneline 2>/dev/null || echo '?')"
echo

echo "--- Gen1 (pcb00003) ---"
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
ls artifacts/oralable_*_pcb00003_merged.hex 2>/dev/null | tail -3 || echo "(no Gen1 merged.hex in artifacts/)"
echo

echo "--- Gen2 (pcb00003_gen2) ---"
if [[ -d boards/byteexplain/pcb00003_gen2 ]]; then
  echo "Board stub: OK"
  [[ -f boards/byteexplain/pcb00003_gen2/VERSION.gen2 ]] && {
    echo -n "VERSION.gen2: "
    tr '\n' ' ' < boards/byteexplain/pcb00003_gen2/VERSION.gen2 | sed 's/ *= */=/g'
    echo
  }
else
  echo "Board stub: MISSING — create boards/byteexplain/pcb00003_gen2"
fi
ls artifacts/oralable_*_pcb00003_gen2_merged.hex 2>/dev/null | tail -3 || echo "(no Gen2 merged.hex yet — expected until G2-P0)"
echo

echo "--- Branches (local) ---"
git branch --list 'feature/gen2*' 'known-good*' 'main' 2>/dev/null || true
echo

echo "--- Tracking docs ---"
TRACK="$ROOT/../cursor_oralable/docs/GEN1_GEN2_TRACKING.md"
if [[ -f "$TRACK" ]]; then
  echo "Tracking: $TRACK"
  rg -n '^\| \*\*G2-P' "$TRACK" 2>/dev/null | head -10 || true
else
  echo "Tracking doc not found at $TRACK"
fi
echo
echo "Workflow: docs/GEN2_GIT_WORKFLOW.md"
echo "Done."
