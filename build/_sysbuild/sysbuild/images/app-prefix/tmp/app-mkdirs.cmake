# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/johnacogan67/tgm_firmware/tgm_firmware/app")
  file(MAKE_DIRECTORY "/Users/johnacogan67/tgm_firmware/tgm_firmware/app")
endif()
file(MAKE_DIRECTORY
  "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/app"
  "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix"
  "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix/tmp"
  "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp"
  "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix/src"
  "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/johnacogan67/tgm_firmware/tgm_firmware/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp${cfgdir}") # cfgdir has leading slash
endif()
