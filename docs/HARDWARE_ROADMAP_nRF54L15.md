# Oralable Hardware Roadmap: nRF54L15 + MAXM86161

**See also:** [ORALABLE_MARKET_LANDSCAPE.md](./ORALABLE_MARKET_LANDSCAPE.md) · [docs/README.md](./README.md)

Source documents (Seed A data room, 2026-06-09):

- `04_TEC_260609_product_hardware_kaga_nrf54_ES4L15BA1_DataSheet_V0_1_Confidential_20251222E.pdf`
- `04_TEC_260609_product_hardware_ppg_sensor_max_specifications_17520358.pdf` (ADI **MAXM86161** datasheet, Rev 1)

## Summary

| Block | **Gen1** (BOM REV8, shipping pilot) | **Gen2** (BOM REV9, REV11) |
|-------|-------------------------------------|----------------------------|
| Wireless MCU | **Kaga ES2832AA2** → nRF52832 | **Kaga ES4L15BA1** → **nRF54L15-CAAA-R** |
| Battery | CG-320B ~15 mAh | **LP260820** 30 mAh |
| PPG | **MAXM86161** (I²C 0x62, R/G/IR) | **MAXM86161EFD+** (same device; formal production spec) |
| Accelerometer | LIS2DTW12 | TBD on new PCB (likely retained) |
| BLE | 5.x / SoftDevice legacy path | **Bluetooth Core 6.0** (nRF Connect SDK) |
| Flash / RAM | 512 KB / 64 KB | **1.5 MB NVM / 256 KB RAM** |

The **MCU migration is the major change**. The PPG sensor is **not** moving to MAX86171/86178 (Oura 4 / WHOOP 5 class); the data-room PPG file confirms continued use of the integrated **MAXM86161** optical module already in firmware.

---

## Kaga ES4L15BA1 module (nRF54L15)

| Parameter | Value |
|-----------|--------|
| Part number | **ES4L15BA1** (chip: nRF54L15-CAAA-R) |
| Package | 28-pin LGA, **3.25 × 8.55 × 1.00 mm** |
| Radio | Bluetooth Core **6.0** |
| NVM / RAM | **1.5 MB / 256 KB** |
| Supply | **1.7–3.6 V** (VDD POR ≥ 1.75 V) |
| TX current | ~9.1 mA @ max power |
| RX current | ~2.1 mA (1M / 2M BLE) |
| System OFF | ~0.7 µA |
| Antenna | **On-module**; PAD11 (OUT_ANT) ↔ PAD12 (OUT_MOD) must be **shorted** for internal antenna |
| SWD | Pin 14 SWDIO, Pin 15 SWDCLK (J-Link compatible) |
| External parts | **4.7 µH** inductor (1608) at DCC; **~100 µF** bulk cap recommended on battery builds |
| GPIO exposed | **15** reconfigurable: P0.03, P0.04, P1.00–P1.08, P2.01, P2.02, P2.04, P2.05 |

### PCB layout constraints (from Kaga antenna note)

- **No copper** in module antenna keep-out zone (all layers).
- **≥ 30 mm** GND reference length from module GND pads.
- Avoid metal/resin directly over the module antenna face.
- Module certified as-is — do not change matching network.

### Implications vs pcb00003

- **Mechanical**: Module is **8.55 mm long** — drives next enclosure/PCB outline (cheek clip must clear antenna keep-out).
- **GPIO budget**: Only **15** pins vs a full nRF52832 GPIO map — pinmux plan required for I²C (PPG + ACC), PPG INT, ACC INT, charger sense, battery enable, LED control.
- **Power**: Regulator must handle **TX load steps**; follow Kaga 100 µF guidance on small LiPo (Oralable CG-320B class).
- **RF**: On-module antenna reduces custom antenna work but **body-worn detuning** (cheek) still needs validation — different from pcb00003 PCB trace antenna.
- **SDK**: Build against **nRF Connect SDK** with `nRF54L15` board support (not nRF52832 `pcb00003`).

---

## MAXM86161 PPG (unchanged silicon)

| Parameter | Value |
|-----------|--------|
| Ordering | **MAXM86161EFD+** (OLGA 2.9 × 4.3 × 1.4 mm) |
| Interface | I²C |
| LEDs | 3× programmable (IR, RED, GRN) — matches R_G_IR cheek coupling |
| ADC | 19-bit charge-integrating |
| FIFO | 128 words |
| VLED | 3.0–5.5 V single supply |
| Sample rate | 8–4096 sps programmable |
| Low-power | Optical readout **< 10 µA typ @ 25 sps** |

### Firmware impact

- Existing `drivers/sensor/maxm86161/` and `app/src/ppg.c` remain the right abstraction.
- Re-validate on new PCB: I²C bus pins, `int-gpios`, LED current settings for **cheek IR-DC coupling (10M–70M raw)**.
- Signal-processing rules unchanged: **50 Hz** resample, Butterworth bandpass, IR-DC muscle occlusion.

---

## Competitive positioning after migration

Oralable moves from **nRF52832 tier** (ArcX, early Polar) to **nRF54L15 tier** (IDO IDR01, next-gen health rings) with **~3× Flash** and **4× RAM** — room for on-device bruxism ML, richer mcumgr OTA, and longer BLE connection parameter negotiation without RAM pressure.

PPG stays on **MAXM86161** (integrated module) rather than discrete **MAX86171/86178** AFE — lower BOM/complexity, proven in current firmware; trade-off is less multi-photodiode flexibility than Oura 4 / Ultrahuman-class designs.

| | pcb00003 (now) | Next gen |
|---|:---:|:---:|
| MCU | nRF52832 | **nRF54L15** (Kaga ES4L15BA1) |
| PPG | MAXM86161 | MAXM86161EFD+ |
| Peer products | ArcX, early Oralable | **IDO IDR01**, future Nordic ring refs |
| WHOOP 4.0 class | Less RAM/Flash | Comparable headroom to nRF52840 strap tier; below WHOOP 5 (Ambiq + NAND) |

---

## Firmware migration checklist

**Detailed change map / roadmap:** [cursor_oralable/docs/GEN1_GEN2_MIGRATION.md](../../cursor_oralable/docs/GEN1_GEN2_MIGRATION.md)  
**Living checklist / timeline:** [cursor_oralable/docs/GEN1_GEN2_TRACKING.md](../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md)  
**Git workflow:** [GEN2_GIT_WORKFLOW.md](./GEN2_GIT_WORKFLOW.md)

1. **Board** — Stub at `boards/byteexplain/pcb00003_gen2/` (branch `feature/gen2-nrf54l15`). Lock DTS pinmux from REV11 netlist, then complete NCS `nrf54l15` includes + partitions.
2. **NCS upgrade** — Confirm workspace NCS version supports nRF54L15 + Bluetooth 6.0 controller.
3. **Pinmux** — Map I²C0/TWIM, PPG INT, ACC INT, charger, battery, LEDs within 15 module GPIOs.
4. **Power tree** — DCC inductor, bulk cap, VLED for MAXM86161 (may need separate rail from 3.3 V module VDD).
5. **RF / BLE** — Re-run connection soak (iOS `connectionTimeout`); antenna on cheek may differ from pcb00003.
6. **MCUboot + OTA** — Re-run sysbuild signing on nRF54L15; confirm Nordic Device Manager path.
7. **Drivers** — Port `lis2dtw12`, `maxm86161`, SAADC battery path to new pins.
8. **Validation** — `scripts/check_ir_dc_scaling.py` on cheek logs; compare to pcb00003 baseline.

---

## References

- Kaga FEI ES4L15BA1 datasheet V0.1 (2025-12-22)
- ADI MAXM86161 datasheet Rev 1 (4/23), ordering MAXM86161EFD+
- [Nordic nRF54L15 product specification](https://docs.nordicsemi.com/)
- [IDO IDR01 + nRF54L15 (Nordic news)](https://www.nordicsemi.com/Nordic-news/2025/10/IDOs-IDR01-smart-ring-integrates-Nordics-nRF54L15-SoC)
- [Appendix A: Nordic wearables](./ORALABLE_MARKET_LANDSCAPE.md#appendix-a-nordic-wearables-comparison)
- [Appendix B: PPG sensors](./ORALABLE_MARKET_LANDSCAPE.md#appendix-b-ppg-sensor-comparison)
- [Market landscape & strategy](./ORALABLE_MARKET_LANDSCAPE.md)
- [OTA via Device Manager](./OTA_DEVICE_MANAGER.md)
