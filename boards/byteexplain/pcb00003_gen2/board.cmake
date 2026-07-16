# SPDX-License-Identifier: Apache-2.0
# Gen2 J-Link: confirm device string for Kaga ES4L15BA1 / nRF54L15 when flashing.

board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
