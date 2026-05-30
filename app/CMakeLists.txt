# -----------------------------------------------------------------------------
# Copyright (c) 2024 WeeGee bv

cmake_minimum_required(VERSION 3.13.1)

# Avoid picking the workspace root as an implicit Zephyr module
# (it contains a nested Zephyr checkout at ./zephyr/), which causes
# recursive Kconfig loading when building this app target.
set(ZEPHYR_MODULES
  "${CMAKE_CURRENT_SOURCE_DIR}/../nrf"
  "${CMAKE_CURRENT_SOURCE_DIR}/../nrfxlib"
  "${CMAKE_CURRENT_SOURCE_DIR}/../modules/hal/nordic"
  "${CMAKE_CURRENT_SOURCE_DIR}/../modules/hal/cmsis"
  "${CMAKE_CURRENT_SOURCE_DIR}/../modules/debug/segger"
  "${CMAKE_CURRENT_SOURCE_DIR}/../modules/crypto/tinycrypt"
  "${CMAKE_CURRENT_SOURCE_DIR}/../module"
  CACHE STRING "Pinned Zephyr modules for app target" FORCE
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_include_directories(app PRIVATE ../include)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)
target_sources(app PRIVATE src/tgm_service.c)
target_sources(app PRIVATE src/ppg.c)
target_sources(app PRIVATE src/acc.c)
target_sources(app PRIVATE src/battery.c)
