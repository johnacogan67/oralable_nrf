cmake_minimum_required(VERSION 3.20.0)

# The repository root formerly carried an experimental application target. It
# is not the release entry point. Keep this explicit fence so a fresh checkout
# cannot silently build stale root/src sources instead of the canonical app/.
message(FATAL_ERROR "The repository root is not a firmware application entry point. For Gen1 use scripts/build_firmware.sh (app/, pcb00003/nrf52832, NCS v2.7.0). For the non-release Gen2 candidate use scripts/build_gen2_app.sh with explicit --ncs-root and --toolchain arguments; see docs/GEN2_PROGRAM_GATE.md.")

# Tell Zephyr to look in this project folder for the 'boards' directory
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(tgm_firmware)

#
# Ensure generated headers exist early enough for out-of-tree driver targets.
# Some Zephyr headers include <offsets.h>, which is generated later in the build.
# Touching it here avoids parallel-build races while the real generator runs.
#
set(INCL_GENERATED_DIR ${CMAKE_BINARY_DIR}/zephyr/include/generated)
file(MAKE_DIRECTORY ${INCL_GENERATED_DIR})
file(TOUCH ${INCL_GENERATED_DIR}/offsets.h)
target_sources(app PRIVATE
    src/main.c
    src/ble.c
    src/ppg.c
    src/acc.c
    src/battery.c
    src/power_profiler.c
    src/tgm_service.c
    drivers/sensor/maxm86161/maxm86161.c
    drivers/sensor/lis2dtw12/lis2dtw12.c
)
target_include_directories(app PRIVATE src)
zephyr_include_directories(include)
add_subdirectory(drivers)
