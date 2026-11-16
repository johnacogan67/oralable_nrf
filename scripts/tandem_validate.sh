#!/bin/bash
# Tandem validation: oralable_nrf + oralable_swift
#
# Runs:
# 1) Canonical Gen1 firmware build check
# 2) iOS build check
# 3) Optionally records a commit pair + status in compatibility_matrix.md
#
# Usage:
#   ./scripts/tandem_validate.sh
# Optional env overrides:
#   ORALABLE_SWIFT_ROOT
#   IOS_SCHEME
#   IOS_WORKSPACE
#   ORALABLE_CORE_ROOT
#   FIRMWARE_BUILD_CMD
# Optional argument:
#   --record  Append a compatibility-matrix row after automated checks pass.

set -euo pipefail

FIRMWARE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ORALABLE_SWIFT_ROOT="${ORALABLE_SWIFT_ROOT:-$HOME/work/oralable_swift}"
ORALABLE_CORE_ROOT="${ORALABLE_CORE_ROOT:-$HOME/work/OralableCore}"
IOS_WORKSPACE="${IOS_WORKSPACE:-$ORALABLE_SWIFT_ROOT/OralableApp/OralableWorkspace.xcworkspace}"
IOS_SCHEME="${IOS_SCHEME:-OralableApp}"
MATRIX_FILE="$FIRMWARE_ROOT/docs/compatibility_matrix.md"

FIRMWARE_BUILD_CMD="${FIRMWARE_BUILD_CMD:-$FIRMWARE_ROOT/scripts/build_firmware.sh --pristine --build-dir build_tandem_pcb00003}"
RECORD=0
if [[ "${1:-}" == "--record" ]]; then
  RECORD=1
  shift
fi
if [[ $# -gt 0 ]]; then
  echo "Usage: $0 [--record]" >&2
  exit 2
fi

ts="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
short_date="$(date -u +"%Y-%m-%d")"

fw_commit="$(/usr/bin/git -C "$FIRMWARE_ROOT" rev-parse --short HEAD)"
ios_commit="$(/usr/bin/git -C "$ORALABLE_SWIFT_ROOT" rev-parse --short HEAD)"

echo "========================================"
echo " Tandem Validation"
echo " UTC:               $ts"
echo " Firmware commit:   $fw_commit"
echo " iOS commit:        $ios_commit"
echo " iOS workspace:     $IOS_WORKSPACE"
echo " Core package:      $ORALABLE_CORE_ROOT"
echo " iOS scheme:        $IOS_SCHEME"
echo "========================================"

status="PASS"
notes="Build checks passed"

echo
echo "[1/2] Firmware build check (pcb00003/nrf52832)"
echo "CMD: $FIRMWARE_BUILD_CMD"
if ! bash -lc "$FIRMWARE_BUILD_CMD"; then
  status="FAIL"
  notes="Firmware build failed"
fi

echo
echo "[2/2] iOS build check"
if [ "$status" = "PASS" ]; then
  if [[ ! -f "$ORALABLE_CORE_ROOT/Package.swift" || ! -d "$IOS_WORKSPACE" ]]; then
    status="FAIL"
    notes="iOS workspace or sibling OralableCore package is missing"
  elif ! /usr/bin/xcodebuild \
      -workspace "$IOS_WORKSPACE" \
      -scheme "$IOS_SCHEME" \
      -configuration Debug \
      -destination "generic/platform=iOS Simulator" \
      build >/tmp/oralable_ios_build.log 2>&1; then
    status="FAIL"
    notes="iOS build failed (see /tmp/oralable_ios_build.log)"
  fi
else
  echo "Skipping iOS build due to earlier failure."
fi

echo
echo "Result: $status"
echo "Notes:  $notes"

manual_col="PENDING (run Device Manager + battery soak)"
row="| $short_date | $fw_commit | $ios_commit | tandem_validate.sh | $manual_col | $status | $notes |"

if [[ "$RECORD" -eq 1 && "$status" = "PASS" && -f "$MATRIX_FILE" ]]; then
  echo "$row" >> "$MATRIX_FILE"
fi

echo
if [[ "$RECORD" -eq 1 && "$status" = "PASS" ]]; then
  echo "Appended to: $MATRIX_FILE"
else
  echo "Not recorded (pass --record after manual validation is planned)."
fi
echo "$row"

if [ "$status" != "PASS" ]; then
  exit 1
fi
