cmake_minimum_required(VERSION 3.20.0)

# Board files live in the oralable_nrf repo, not in the NCS tree.
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

list(APPEND DTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(oralable_gen2_ble)

target_sources(app PRIVATE src/main.c)
