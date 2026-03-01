# Power Profiler & Battery Tracking Integration

**Project:** POWER_PROFILER_INTEGRATION (2026-02-27)  
**Target:** REV10 (nRF52832) with CG-320B (15 mAh) battery

## Summary

This integration adds state-aware power modeling and medical-grade battery telemetry to the Oralable MAM firmware.

## Implemented Components

### 1. Power Profiler (`power_profiler.h` / `power_profiler.c`)

- **States:** `IDLE` → `ADV` (25 µA) → `CONN` (50 µA) → `STREAM` (1.4 mA)
- **mAh integration:** `mAh += (current_A × duration_ms) / 3_600_000`
- **Hooks:**
  - `ble.c`: `POWER_STATE_CONN` on connect, `POWER_STATE_ADV` on disconnect
  - `tgm_service.c`: `POWER_STATE_STREAM` when PPG or Accel notifications enabled
- **API:** `power_profiler_get_total_mah()`, `power_profiler_get_remaining_minutes()`, `power_profiler_get_telemetry()`

### 2. Battery (`battery.c`)

- **CG-320B curve:** Linear 4.35 V (100%) → 3.0 V (0%)
- **Smoothing:** Rolling 3-sample max to reduce percent jumps during LED pulses
- **Critical low shutdown:** At 2.8 V (configurable via `CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN`)
- **SAADC:** Uses devicetree config (12-bit, 0.6 V internal ref). PCB00003 uses `ADC_GAIN_1`. For 1/6 gain on REV10, add a board overlay (see below).

### 3. BatteryStats Characteristic (3A0FF00A)

- **Payload:** `[Voltage_mV, Percent, Remaining_Minutes]` (9 bytes)
- **Notify:** Every 60 s when subscribed

## Configuration

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN` | Shutdown at 2.8 V to protect cell | `y` |
| `CONFIG_REBOOT` | Enable `sys_reboot()` for shutdown | `y` |

## REV10 ADC Overlay (Optional)

For 1/6 gain and 0.6 V reference on REV10, create `boards/byteexplain/pcb00003/overlay-rev10.conf`:

```
# REV10 SAADC: 1/6 gain, 0.6 V ref for 4.35 V peak (voltage divider 11)
# Add overlay in devicetree:
# &adc { channel@0 { zephyr,gain = "ADC_GAIN_1_6"; }; };
```

And a corresponding `.overlay` file if the DTS needs to override the ADC channel.

## Verification

1. **Full discharge test:** Run one unit to shutdown and confirm software mAh matches ~15 mAh.
2. **Sample batching:** If streaming draw exceeds 1.5 mA, consider 5:1 batching in `ppg.c` to extend runtime beyond 8 h.

## Clarification Responses

1. **ADC lookup table:** Rolling 3-sample max is used instead of a lookup table to smooth percent during LED pulses. A full discharge-curve LUT can be added if needed.
2. **Critical low shutdown:** Implemented at 2.8 V via `CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN`; disable in `prj.conf` if not desired.
