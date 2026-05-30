# Build entry point when oralable_nrf is discovered as a Zephyr module.
# Keep this minimal to avoid pulling the repository root application
# CMakeLists.txt when building /app as the primary target.

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../drivers
                 ${CMAKE_CURRENT_BINARY_DIR}/drivers)
