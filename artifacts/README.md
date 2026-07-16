# Firmware artifacts — naming

| Generation | Board | Pattern |
|------------|-------|---------|
| **Gen1** | `pcb00003` | `oralable_<X.Y.Z>_pcb00003_{merged.hex,app_update.bin,dfu_application.zip}` |
| **Gen2** | `pcb00003_gen2` | `oralable_<X.Y.Z>_pcb00003_gen2_{merged.hex,app_update.bin,dfu_application.zip}` |

Examples:

- `oralable_1.0.66_pcb00003_merged.hex` — Gen1 pilot
- `oralable_2.0.0_pcb00003_gen2_merged.hex` — Gen2 (after G2-P0+)

Do not overwrite Gen1 hex files with Gen2 builds. Copy into `cursor_oralable/docs/data_room/firmware/` only after tag + nRF Connect verify.

