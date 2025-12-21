# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/johnacogan67/Projects/bootloader/mcuboot/boot/zephyr")
  file(MAKE_DIRECTORY "/Users/johnacogan67/Projects/bootloader/mcuboot/boot/zephyr")
endif()
file(MAKE_DIRECTORY
  "/Users/johnacogan67/Projects/tgm_firmware/build/mcuboot"
  "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix"
  "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix/tmp"
  "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix/src/mcuboot_subimage-stamp"
  "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix/src"
  "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix/src/mcuboot_subimage-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix/src/mcuboot_subimage-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/johnacogan67/Projects/tgm_firmware/build/modules/mcuboot/mcuboot_subimage-prefix/src/mcuboot_subimage-stamp${cfgdir}") # cfgdir has leading slash
endif()
