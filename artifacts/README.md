# Firmware artifacts (nRF)

**Keep in this folder:** current **1.0.84** (N), rollback **1.0.82** (N−1), and deep-recovery **1.0.70**.

Data-room pack (`cursor_oralable/docs/data_room/firmware/`) ships **1.0.84 + 1.0.82** only — see `FIRMWARE_ROLLBACK.md`.

Older builds belong in GitHub Releases, not this tree.

**Gen 2 (share with Wout, 26 Oct 2026):** board `pcb00003_gen2`, nRF54L15, MCUboot version **1.0.87+0**. Not a kit release. PPG and ACC do not stream on the boards in hand.

| Use | File |
|-----|------|
| SWD | `oralable_gen2_ota_merged.hex` |
| Device Manager | `oralable_gen2_dfu_application.zip` |

Do not flash the Gen 1 files in this folder onto REV11.
