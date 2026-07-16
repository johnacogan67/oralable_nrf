# Oralable firmware documentation index

Canonical docs for **oralable_nrf** (pcb00003, nRF52832 → nRF54L15 roadmap).  
**Strategy hub:** [ORALABLE_MARKET_LANDSCAPE.md](./ORALABLE_MARKET_LANDSCAPE.md) (v1.2, June 2026). **Doc pack:** `docs/VERSION` → **1.2.0**.

## Product & market

| Document | Description |
|----------|-------------|
| [ORALABLE_MARKET_LANDSCAPE.md](./ORALABLE_MARKET_LANDSCAPE.md) | Positioning, competitors, regulatory path, Android, GTM, longitudinal monitoring |
| [Appendix A: Nordic wearables](./ORALABLE_MARKET_LANDSCAPE.md#appendix-a-nordic-wearables-comparison) | MCU tiers, rings/straps, EMG wearables (formerly separate doc) |
| [Appendix B: PPG sensors](./ORALABLE_MARKET_LANDSCAPE.md#appendix-b-ppg-sensor-comparison) | MAXM86161 vs JCRing, IDO, Polar, Withings, Oura, WHOOP, Wellue |

## Hardware & roadmap

| Document | Description |
|----------|-------------|
| [HARDWARE_ROADMAP_nRF54L15.md](./HARDWARE_ROADMAP_nRF54L15.md) | Kaga ES4L15BA1 module, Gen2 migration checklist |
| [GEN2_GIT_WORKFLOW.md](./GEN2_GIT_WORKFLOW.md) | Branches, tags, VERSION, no-fork policy |
| [GEN1_GEN2_TRACKING.md](../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md) | Living timeline + G2-P0…P6 checklist |
| [GEN1_GEN2_MIGRATION.md](../../cursor_oralable/docs/GEN1_GEN2_MIGRATION.md) | Capabilities, BOM delta, firmware map |
| [boards/byteexplain/pcb00003/README.md](../boards/byteexplain/pcb00003/README.md) | Gen1 shipping board (nRF52832) |
| [boards/byteexplain/pcb00003_gen2/README.md](../boards/byteexplain/pcb00003_gen2/README.md) | Gen2 board stub (nRF54L15 / REV11) |

## Firmware operations

| Document | Description |
|----------|-------------|
| [DEVELOPMENT.md](./DEVELOPMENT.md) | Tandem workflow, smoke checklist, compatibility matrix (GATT/LED detail → system hub) |
| [OTA_DEVICE_MANAGER.md](./OTA_DEVICE_MANAGER.md) | MCUboot + mcumgr OTA via Nordic Device Manager |
| [../README.md](../README.md) | Build, flash (`flash_and_rtt.sh`), GATT parsing |

## Cross-repo

| Repo | Docs |
|------|------|
| Repo | Docs |
|------|------|
| **cursor_oralable** | [ORALABLE_SYSTEM_ARCHITECTURE.md](../../cursor_oralable/docs/ORALABLE_SYSTEM_ARCHITECTURE.md), algorithms — `docs/README.md` |
| **oralable_swift** | `LAUNCH_READINESS_CHECKLIST.md`, `docs/MOBILE_APP_FLOWS.md`, `CLOUDKIT_PRODUCTION_SETUP.md` |
| **OralableCore** | BLE models, `ProfessionalHandshakeExport`, algorithms (Swift package) |

## Current shipping snapshot (pcb00003)

| Item | Value |
|------|--------|
| Firmware | **`app/VERSION`** (e.g. 1.0.37-nrfconnect); bench validation matrix → [system architecture §3](../../cursor_oralable/docs/ORALABLE_SYSTEM_ARCHITECTURE.md#3-validation-status-matrix-where-we-are) |
| iOS minimum | **1.0.36** (`FirmwareGate`) |
| Board | `pcb00003/nrf52832` |
| First SWD flash | `build_pcb00003/merged.hex` |
| OTA artifact | `build_pcb00003/dfu_application.zip` |

*Last updated: June 2026*
