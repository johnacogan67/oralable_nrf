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
| [HARDWARE_ROADMAP_nRF54L15.md](./HARDWARE_ROADMAP_nRF54L15.md) | Kaga ES4L15BA1 module, nRF54L15 migration checklist |
| [boards/byteexplain/pcb00003/README.md](../boards/byteexplain/pcb00003/README.md) | Current shipping board |

## Firmware operations

| Document | Description |
|----------|-------------|
| [DEVELOPMENT.md](./DEVELOPMENT.md) | Firmware + iOS tandem workflow, smoke checklist, **LED/battery**, compatibility matrix |
| [OTA_DEVICE_MANAGER.md](./OTA_DEVICE_MANAGER.md) | MCUboot + mcumgr OTA via Nordic Device Manager |
| [../README.md](../README.md) | Build, flash (`flash_and_rtt.sh`), GATT parsing |

## Cross-repo

| Repo | Docs |
|------|------|
| **cursor_oralable** | Algorithms, clinical protocol, IR-DC — `docs/README.md` |
| **oralable_swift** | `LAUNCH_READINESS_CHECKLIST.md`, `docs/MOBILE_APP_FLOWS.md`, `CLOUDKIT_PRODUCTION_SETUP.md` |
| **OralableCore** | BLE models, `ProfessionalHandshakeExport`, algorithms (Swift package) |

## Current shipping snapshot (pcb00003)

| Item | Value |
|------|--------|
| Firmware | **1.0.36-nrfconnect** (`app/VERSION`) |
| iOS minimum | **1.0.36** (`FirmwareGate`) |
| Board | `pcb00003/nrf52832` |
| First SWD flash | `build_pcb00003/merged.hex` |
| OTA artifact | `build_pcb00003/dfu_application.zip` |

*Last updated: June 2026*
