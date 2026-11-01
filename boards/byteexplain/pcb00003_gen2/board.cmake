# SPDX-License-Identifier: Apache-2.0
# Same J-Link path as Gen 1. NCS 2.7 names this SoC cortex-m33.

board_runner_args(jlink "--device=cortex-m33" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
