# Pin Zephyr module discovery for sysbuild child images.
# Without this, the workspace root is auto-scanned and Kconfig recursion breaks mcuboot.

set(_repo "${CMAKE_CURRENT_LIST_DIR}/..")

set(_base_modules
  "${_repo}/nrf"
  "${_repo}/nrfxlib"
  "${_repo}/bootloader/mcuboot"
  "${_repo}/modules/hal/nordic"
  "${_repo}/modules/hal/cmsis"
  "${_repo}/modules/debug/segger"
  "${_repo}/modules/crypto/tinycrypt"
  "${_repo}/modules/crypto/mbedtls"
  "${_repo}/modules/lib/zcbor"
)

set(_app_modules "${_base_modules};${_repo}/module")
set(_boot_modules "${_base_modules}")

set(${DEFAULT_IMAGE}_BOARD_ROOT "${_repo}" CACHE PATH "Oralable board root" FORCE)
# Expose repo-root dts/bindings so GPIO_DT_SPEC_GET(..., int_gpios) works.
set(${DEFAULT_IMAGE}_DTS_ROOT "${_repo}" CACHE PATH "Oralable dts root" FORCE)
set(${DEFAULT_IMAGE}_ZEPHYR_MODULES "${_app_modules}" CACHE STRING "Oralable app modules" FORCE)

set(ZEPHYR_MCUBOOT_MODULE_DIR "${_repo}/bootloader/mcuboot" CACHE PATH "MCUboot module dir" FORCE)
set(mcuboot_ZEPHYR_MODULES "${_boot_modules}" CACHE STRING "MCUboot modules" FORCE)
