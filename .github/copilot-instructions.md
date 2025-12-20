<!-- .github/copilot-instructions.md - guidance for AI coding agents -->
# Copilot / AI agent instructions — tgm_firmware

Purpose: help an AI be immediately productive in this Zephyr / nRF Connect SDK firmware repository.

- **Big picture**: This repo is an nRF Connect (Zephyr) based firmware workspace. The main application lives in `tgm_firmware/app`. The project uses `west` to manage the nRF SDK (see `tgm_firmware/west.yml`) and follows Zephyr conventions (Kconfig/prj.conf, CMake module layout).

- **Key locations**:
  - Application: `tgm_firmware/app/`
  - Public headers & service API: `tgm_firmware/app/src/` (example: `tgm_service.h`)
  - Drivers: `tgm_firmware/app/drivers/` (examples: `maxm86161`, `lis2dtw12`)
  - Workspace manifest: `tgm_firmware/west.yml`
  - Module CMake glue: `tgm_firmware/CMakeLists.txt`

- **Architecture summary (how code fits together)**:
  - Sensor drivers live in `app/drivers` and expose small structs (`ppg_sample`, `acc_sample`).
  - Bluetooth GATT layer and framing are implemented in `app/src/tgm_service.h` / `.c` — UUIDs, notify functions, and frame layout are defined here. Don't change UUIDs without coordination.
  - Build configuration and feature flags are controlled via Zephyr Kconfig `prj.conf` entries (see references to `CONFIG_PPG_SAMPLES_PER_FRAME`, `CONFIG_ACC_SAMPLES_PER_FRAME`).

- **Build / run / debug workflows (concrete commands)**:
  - Initialize workspace (one-time):

    west init -m https://github.com/johna67/tgm_firmware --mr main tgm_firmware
    cd tgm_firmware
    west update

  - Build app for a board (example):

    cd tgm_firmware
    west build -b <board-name> app

  - Flash: `west flash` (or use the nRF Connect VSCode extension as described in `tgm_firmware/README.md`).
  - Debugging: use the nRF Connect extension or `west build` + `west debug`; when using the extension select optimization level "Optimize for debugging -Og".

- **Project-specific conventions and patterns**:
  - Prefixes: public API and types use `tgm_` (e.g., `tgm_service_cb`, `tgm_service_send_ppg_notify`). Follow this naming.
  - Kconfig: feature flags use `CONFIG_...` and sample/frame sizes are defined in `prj.conf` and referenced in headers (e.g., `CONFIG_PPG_SAMPLES_PER_FRAME`).
  - Driver includes use angled include path like `#include <app/drivers/maxm86161.h>` (keep include paths consistent with `zephyr_include_directories` in `CMakeLists.txt`).
  - Bluetooth framing: `tgm_service.h` documents exact byte layout for PPG/ACC frames; any client-facing changes must match that layout.

- **Integration points / external dependencies**:
  - The repo depends on nRF Connect SDK modules declared in `west.yml`. If adding external projects, update `west.yml` and run `west update`.
  - CI workflows live under `zephyr/.github/workflows` (e.g., bsim tests); follow patterns there for test/CI changes.

- **Concrete code examples (copy/paste friendly)**:
  - Add a new PPG field to frames: add field type in `app/src/tgm_service.h`, increment frame_size usage, and update `tgm_service_send_ppg_notify` implementation to populate bytes.
  - Change samples-per-frame: edit `prj.conf` in the application directory and rebuild; do not hardcode sample counts in source files.

- **What NOT to do**:
  - Do not modify the `west.yml` manifest lightly — adding/removing SDK modules must be done via `west` and tested with `west update`.
  - Do not reuse or duplicate Bluetooth UUIDs defined in `tgm_service.h`.

If anything here is unclear or you want more/less detail (examples of adding a characteristic, a patch for a driver, or a build matrix for CI), tell me which section to expand. 
