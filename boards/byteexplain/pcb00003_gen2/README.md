# PCB00003 Gen2 (REV11 / BOM REV9)

**Hardware gen:** Gen2 · **Module:** Kaga **ES4L15BA1** → Nordic **nRF54L15** · **Cell in hand (14 Oct 2026):** LP270829 35 mAh

**Port of Gen 1.** Use the Gen 1 application (`app/`) and `prj.conf`. This directory is the board file only: same nets, nRF54 pins. Do not start a second protocol.

| Item | Value |
|------|--------|
| Board target | `pcb00003_gen2` |
| Bring-up branch | `feature/gen2-nrf54l15` |
| Gen1 board (frozen source) | `pcb00003` / nRF52832 |
| Status | **Port.** Pins not locked. Do not flash yet. |

**Docs:**  
- [GEN1_GEN2_TRACKING.md](../../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md) (timeline + checklist)  
- [PCB00003_GEN2_REV11_HARDWARE.md](../../../cursor_oralable/docs/PCB00003_GEN2_REV11_HARDWARE.md)  
- [HARDWARE_ROADMAP_nRF54L15.md](../../docs/HARDWARE_ROADMAP_nRF54L15.md)  
- [GEN2_GIT_WORKFLOW.md](../../docs/GEN2_GIT_WORKFLOW.md)

## Install (same path as Gen 1)

First image is an RTT hello. It does not drive power or sensor pins. Hex: `artifacts/oralable_gen2_rtt_hello.hex` (built from `bringup/`). SWD program, then RTT. Same J-Link serial as Gen 1 (`1050090445`). RTT block: `0x200003c0`.

```bash
cd oralable_nrf
nrfjprog --snr 1050090445 --program artifacts/oralable_gen2_rtt_hello.hex --sectorerase --verify --reset
JLinkRTTLogger -Device Cortex-M33 -If SWD -Speed 4000 -RTTChannel 0 -RTTAddress 0x200003c0 artifacts/rtt_gen2.log
```

The full Gen 1 app is not this image. Sensor pins are still unmapped.

## nRF Connect advertiser

`bringup_ble/` is a bench image. It advertises as **Oralable-G2**, accepts a connection, and exposes one read characteristic that returns `gen2`. It drives **BATEN** from the board file (P1.04) and leaves I²C and the battery ADC off. It uses the internal 32 kHz RC. It is not the Oralable GATT and not a kit version.

The board selects the production nRF54L15, so this image builds with NCS 2.9.3 (`/Users/johnacogan67/ncs/v2.9.3`), not the NCS 2.7 tree in this repo. The programmer is the nRF54L15 DK, J-Link serial `1057765387`, through **P19** debug out and the Tag-Connect on **J1**. Seat the MAM on the charger. The charger is the only power source. P19 pin 3 (SWD0_SELECT) must be grounded by the 10-pin lead so the debugger leaves the kit’s own chip alone. Before programming, the kit should still advertise **Nordic_LBS**. If that name disappears, the lead is on the kit. Stop.

```bash
TC=/opt/nordic/ncs/toolchains/b8efef2ad5
export PATH="$TC/bin:$PATH"
export ZEPHYR_SDK_INSTALL_DIR="$TC/opt/zephyr-sdk"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_BASE=/Users/johnacogan67/ncs/v2.9.3/zephyr
cd /Users/johnacogan67/ncs/v2.9.3
west build --no-sysbuild -b pcb00003_gen2/nrf54l15/cpuapp \
  -d /Users/johnacogan67/ncs/build_gen2_ble \
  /Users/johnacogan67/work/oralable_nrf/bringup_ble \
  -- -DBOARD_ROOT=/Users/johnacogan67/work/oralable_nrf
```

Hex: `artifacts/oralable_gen2_ble.hex`.

**Return point, 23 Oct 2026.** nRF Connect shows **Oralable-G2** with the MAM on the battery. The charger and the debugger were both disconnected. This is the image to put back. The hex in `artifacts/oralable_gen2_ble.hex` is a later rebuild of the same `bringup_ble` tree (larger log stack, advertising retry). The bytes that were on the air are the earlier program of that tree. Reflash `bringup_ble`. Do not overwrite this hex with the sensor application.

```bash
nrfutil device program --firmware artifacts/oralable_gen2_ble.hex --serial-number 1057765387
nrfutil device reset --serial-number 1057765387
```

Unplug the Tag-Connect after reset. In nRF Connect, **Nordic_LBS** is the kit and **Oralable-G2** is the MAM. RTT for this NCS 2.9.3 image is `_SEGGER_RTT` at `0x20000410`.

Artifact naming: `artifacts/oralable_2.0.0_pcb00003_gen2_merged.hex`

## Remove the I2C pin scan

**25 Oct 2026.** Remove the one-shot I2C pin scan before this image is the one to keep. The scan is `app/src/i2c_pin_scan.c`, the header `app/src/i2c_pin_scan.h`, the source line in `app/CMakeLists.txt`, the call in `app/src/main.c`, and the replay in `app/src/tgm_service.c`. Wout confirmed the REV10 pins for REV11: SDA P1.07, SCL P1.03, SENS_EN P1.08, with 10 kΩ pull-ups on the SENS_EN net. The scan can stay on the boards already flashed while the sensor-rail voltages are measured.

## Pinmux

Nets are confirmed (`SDA`, `SCL`, `INT_ACC`, `INT_OPT`, `SENS_EN`, `CHRSTS`, `BATEN`, `BATVOL`).  
**GPIO numbers are TBD** until Altium netlist or first-board continuity — see hardware doc §5. Do not treat `pcb00003_gen2.dts` placeholders as production truth.

## Phase gates

See tracking board **G2-P0 … G2-P6**. Next session is G2-P0 on the arrived MAM: SWD, device id, RTT hello, via `flash_and_rtt.sh`. Do not flash the placeholder pins.
