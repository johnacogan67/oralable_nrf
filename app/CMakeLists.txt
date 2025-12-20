# -----------------------------------------------------------------------------
# Copyright (c) 2024 WeeGee bv

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)
target_sources(app PRIVATE src/tgm_service.c)
target_sources(app PRIVATE src/ppg.c)
target_sources(app PRIVATE src/acc.c)
target_sources(app PRIVATE src/battery.c)
