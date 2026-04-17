# OTA images (pcb00003 / nrf52832)

Build outputs are copied here after `west build` (see commands below).

## Files

| File | Use |
|------|-----|
| `tgm-pcb00003-dfu_application.zip` | **Recommended for phone / nRF Connect Device Manager** — Nordic DFU package (signed application + manifest). |
| `tgm-pcb00003-app_update.bin` | **MCUmgr / `mcumgr` CLI** image upload (`img_mgmt`), same signed payload as the zip’s application image. |

## Rebuild (from your NCS west workspace root, e.g. `~/work`)

```bash
west build -b pcb00003 -d oralable_nrf/build_pcb00003 oralable_nrf
cp oralable_nrf/build_pcb00003/zephyr/dfu_application.zip oralable_nrf/ota_release/tgm-pcb00003-dfu_application.zip
cp oralable_nrf/build_pcb00003/zephyr/app_update.bin oralable_nrf/ota_release/tgm-pcb00003-app_update.bin
```

**Note:** On some machines a *pristine* build directory can hit a Ninja race (`offsets.h` missing) with the deprecated child-image MCUboot flow. If that happens, reuse an existing `build_pcb00003` once configured, or build twice.

## This build

- `CONFIG_BT_PERIPHERAL_PREF_TIMEOUT=300` → **3 s** supervision timeout (10 ms units).
- `CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS=y` with min/max interval preferences per `prj.conf`.
