# PCB00003-NEU_SENSOR board

`PCB00003-NEU_SENSOR` — Oralable shipping board (cheek / masseter clip).

| Item | Value |
|------|--------|
| PCB | **PCB00003-TGM** (clip + case, one BOM per rev) |
| BLE module (Gen1) | **Kaga ES2832AA2** → nRF52832 (**BOM REV8**) |
| BLE module (Gen2) | **Kaga ES4L15BA1** → nRF54L15 (**BOM REV9**, REV11) |
| PPG | MAXM86161 @ I²C `0x62` |
| ACC | LIS2DTW12 @ `0x19` |
| BLE service | TGM `3A0FF000` |
| Firmware | **1.0.82** ship (see `app/VERSION`) |

Hardware design ported from NEU/TGM sensor to Altium; board files based on nRF52-DK patterns.

**Documentation:** [docs/README.md](../../docs/README.md) · [ORALABLE_MARKET_LANDSCAPE.md](../../docs/ORALABLE_MARKET_LANDSCAPE.md) · [HARDWARE_ROADMAP_nRF54L15.md](../../docs/HARDWARE_ROADMAP_nRF54L15.md)

**Flash:** `../../scripts/flash_and_rtt.sh` → `build_pcb00003/merged.hex` (first time) · OTA via [OTA_DEVICE_MANAGER.md](../../docs/OTA_DEVICE_MANAGER.md)
