cmake_minimum_required(VERSION 3.20.0)

# Tell Zephyr to look in this project folder for the 'boards' directory
set(BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(tgm_firmware)

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