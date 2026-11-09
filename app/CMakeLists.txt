# -----------------------------------------------------------------------------
# Copyright (c) 2024 WeeGee bv

cmake_minimum_required(VERSION 3.13.1)

# This is the sole Gen1 release application entry point. The same source can be
# compiled as a Gen2 non-release candidate only through build_gen2_app.sh with
# its separately pinned NCS v2.9.3 workspace and matching toolchain.
# BOARD is not always set when this file starts, so include the build directory
# in the guard to prevent reuse of a Gen2 build tree.
# build_gen2_app.sh writes build_gen2_candidate/, which does not contain the
# board name. Pinning the repo's NCS v2.7 modules into that cache makes Kconfig loop.
set(ORALABLE_BUILD_HINT "${BOARD}${CACHED_BOARD}${CMAKE_BINARY_DIR}$ENV{ZEPHYR_BASE}")
if(NOT "${ORALABLE_BUILD_HINT}" MATCHES "pcb00003_gen2|nrf54l15|gen2_|v2\\.9")
  # Avoid picking the workspace root as an implicit Zephyr module (it contains a
  # nested Zephyr checkout at ./zephyr/), which causes recursive Kconfig loading.
  set(ZEPHYR_MODULES
      "${CMAKE_CURRENT_SOURCE_DIR}/../nrf"
      "${CMAKE_CURRENT_SOURCE_DIR}/../nrfxlib"
      "${CMAKE_CURRENT_SOURCE_DIR}/../bootloader/mcuboot"
      "${CMAKE_CURRENT_SOURCE_DIR}/../modules/hal/nordic"
      "${CMAKE_CURRENT_SOURCE_DIR}/../modules/hal/cmsis"
      "${CMAKE_CURRENT_SOURCE_DIR}/../modules/debug/segger"
      "${CMAKE_CURRENT_SOURCE_DIR}/../modules/crypto/tinycrypt"
      "${CMAKE_CURRENT_SOURCE_DIR}/../modules/crypto/mbedtls"
      "${CMAKE_CURRENT_SOURCE_DIR}/../modules/lib/zcbor"
      "${CMAKE_CURRENT_SOURCE_DIR}/../module"
  )
  set(ZEPHYR_MODULES "${ZEPHYR_MODULES}"
    CACHE STRING "Pinned Zephyr modules for Gen1 app target" FORCE
  )
endif()

# Gen2 keeps the NCS v2.9.3 module list and adds only the Oralable driver
# module. That module brings in MAXM86161 and LIS2DTW12.
if("${ORALABLE_BUILD_HINT}" MATCHES "pcb00003_gen2|nrf54l15|gen2_|v2\\.9")
  list(APPEND EXTRA_ZEPHYR_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../module")
endif()

# Repo-root dts/bindings (adi,maxm86161 / st,lis2dtw12 int-gpios) must be on
# DTS_ROOT or GPIO_DT_SPEC_GET(..., int_gpios) has no generated macros.
list(APPEND DTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_include_directories(app PRIVATE ../include)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ble.c)
target_sources(app PRIVATE src/tgm_service.c)
target_sources(app PRIVATE src/ppg.c)
target_sources(app PRIVATE src/acc.c)
target_sources(app PRIVATE src/battery.c)
target_sources(app PRIVATE src/power_profiler.c)
target_sources(app PRIVATE src/battery_led_indicator.c)
target_sources(app PRIVATE src/charge_detector.c)
target_sources(app PRIVATE src/sensor_i2c.c)
target_sources(app PRIVATE src/early_rtt.c)
target_sources(app PRIVATE src/i2c_pin_scan.c)

# Gen 2 reads boards/.../VERSION.gen2. app/VERSION stays the Gen 1 number.
# Zephyr always generates app_version.h from app/VERSION, so this stamp runs
# after that and rewrites the header before main.c and tgm_service.c compile.
if("${ORALABLE_BUILD_HINT}" MATCHES "pcb00003_gen2|nrf54l15|gen2_|v2\\.9")
  if("$ENV{ZEPHYR_BASE}" STREQUAL "")
    message(FATAL_ERROR "ZEPHYR_BASE is required to stamp the Gen 2 version")
  endif()
  set(GEN2_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../boards/byteexplain/pcb00003_gen2/VERSION.gen2")
  set(GEN2_SIGN_CONF "${CMAKE_CURRENT_SOURCE_DIR}/boards/pcb00003_gen2_nrf54l15_cpuapp.conf")
  set(GEN2_APP_VERSION_H "${CMAKE_BINARY_DIR}/zephyr/include/generated/zephyr/app_version.h")
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gen2_version.stamp
    COMMAND ${CMAKE_COMMAND} -DZEPHYR_BASE=$ENV{ZEPHYR_BASE}
            -DOUT_FILE=${GEN2_APP_VERSION_H}
            -DVERSION_TYPE=APP
            -DVERSION_FILE=${GEN2_VERSION_FILE}
            -P $ENV{ZEPHYR_BASE}/cmake/gen_version_h.cmake
    COMMAND ${CMAKE_COMMAND} -DHEADER=${GEN2_APP_VERSION_H}
            -DSIGN_CONF=${GEN2_SIGN_CONF}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen2_version_string.cmake
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/gen2_version.stamp
    DEPENDS ${GEN2_VERSION_FILE} ${GEN2_SIGN_CONF} ${GEN2_APP_VERSION_H}
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen2_version_string.cmake
    COMMENT "Gen 2 version from the MCUboot sign version"
    VERBATIM
  )
  add_custom_target(gen2_app_version DEPENDS ${CMAKE_BINARY_DIR}/gen2_version.stamp)
  add_dependencies(app gen2_app_version)
  set_source_files_properties(
    src/main.c
    src/tgm_service.c
    $ENV{ZEPHYR_BASE}/../nrf/lib/boot_banner/banner.c
    PROPERTIES OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/gen2_version.stamp)
endif()
