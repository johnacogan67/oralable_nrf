#!/bin/bash
# Build Oralable firmware and produce OTA (app_update.bin) file
# Requires: nRF Connect SDK, west workspace with Zephyr
#
# Usage:
#   ./scripts/build_ota.sh
#   # or from nRF Connect for VS Code: Build → the OTA file is at:
#   #   build/zephyr/app_update.bin

set -e

BOARD="${1:-byteexplain/pcb00003}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Building for board: $BOARD"
echo "Repo root: $REPO_ROOT"

# Try west build (from nRF Connect SDK workspace)
if command -v west &>/dev/null; then
    cd "$REPO_ROOT"
    west build -b "$BOARD" app --pristine 2>/dev/null || west build -b "$BOARD" app
else
    echo "West not found. Use nRF Connect for VS Code:"
    echo "  1. Add existing application: $REPO_ROOT"
    echo "  2. Build for board: $BOARD"
    echo "  3. OTA file: build/zephyr/app_update.bin"
    exit 1
fi

OTA_FILE="$REPO_ROOT/build/zephyr/app_update.bin"
if [[ -f "$OTA_FILE" ]]; then
    echo ""
    echo "=========================================="
    echo "OTA file ready: $OTA_FILE"
    echo "=========================================="
    ls -la "$OTA_FILE"
else
    echo "Build may have succeeded but app_update.bin not found."
    exit 1
fi
