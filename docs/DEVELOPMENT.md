# Oralable Development Workflow (firmware + iOS)

Keeps `oralable_nrf` and `oralable_swift` synchronized. **Doc index:** [README.md](./README.md) · **Product:** [ORALABLE_MARKET_LANDSCAPE.md](./ORALABLE_MARKET_LANDSCAPE.md)

---

## Goals

- Develop `oralable_nrf` and `oralable_swift` in parallel.
- Validate compatibility after each meaningful change.
- Record known-good firmware/iOS commit pairs for quick rollback.

## Source of truth

| Item | Path |
|------|------|
| Firmware | `~/work/oralable_nrf` |
| iOS | `~/work/oralable_swift` |
| Validation script | `oralable_nrf/scripts/tandem_validate.sh` |
| Flash + RTT | `oralable_nrf/scripts/flash_and_rtt.sh` |
| OTA | [OTA_DEVICE_MANAGER.md](./OTA_DEVICE_MANAGER.md) |

---

## Validation loop

1. Make changes in firmware and/or iOS.
2. Run tandem validation:

   ```bash
   cd ~/work/oralable_nrf
   ./scripts/tandem_validate.sh
   ```

3. Complete manual BLE smoke checks (below).
4. Add a row to **Compatibility matrix** (bottom of this doc).
5. Commit in each repo and push.

---

## Manual smoke checklist (TGM / firmware ≥ 1.0.37)

- [ ] Charger + J-Link: boots and advertises as **Oralable**.
- [ ] Battery-only: remains alive ≥ 180s (optional soak).
- [ ] **Device Manager** or **nRF Connect**: TGM `3A0FF000` + SMP services discovered.
- [ ] Read firmware `3A0FF006` → **1.0.37-nrfconnect** (iOS `FirmwareGate` minimum remains **1.0.36**).
- [ ] GATT includes **`3A0FF00A`** (fw log), **`3A0FF00B`** (config write), **`3A0FF00C`** (config state).
- [ ] **Off-body:** PPG/ACC during **10 s connect probe** then gated; status/battery still notify.
- [ ] **On-body cheek:** PPG/ACC notifies after CCC enable; export nRF CSV if comparing to iOS.
- [ ] iOS: Settings → Developer → **Dump firmware diagnostics** → export nRF CSV with `[FW]` lines.
- [ ] iOS app: connect → auto-record; optional CloudKit share smoke.
- [ ] OTA: `dfu_application.zip` after one-time `merged.hex` flash.

**Notes:** RTT optional for pass/fail. Primary acceptance: stable on-body BLE ≥ 2 min, correct worn gating.

---

## LED indicators & battery (pcb00003)

Off-body status uses the **MAXM86161 PPG LEDs** (green / red channels), not a separate status LED. Implementation: `app/src/battery_led_indicator.c`.

### LED patterns (device **not worn**)

Worn detection uses die temperature (**> 25.5°C** → worn). When worn, battery indicator LEDs stop; PPG uses red/IR for sensing.

| Power | Battery (firmware %) | LED |
|-------|----------------------|-----|
| **On charger** | > 80% | **Solid green** |
| **On charger** | ≤ 80% | **Flashing green** (500 ms) |
| **Off charger** | > 80% | **Solid red** |
| **Off charger** | ≤ 80% | **Flashing red** (500 ms) |

**Full threshold:** `BATTERY_LED_FULL_THRESHOLD_PCT` = **80** (`battery_led_indicator.h`).

**Percent mapping:** CG-320B linear fit — 4.35 V = 100%, 3.0 V = 0% (`battery.c`).

### What users often see

| Observation | Likely meaning |
|-------------|----------------|
| Flash **green** on dock, never solid green | Charging but still **≤ 80%** — leave on charger longer (small 15 mAh cell). |
| **Solid red** off dock | Off dock and firmware reads **> 80%** (can be a stale/high ADC reading on a weak cell). |
| **Flashing red** off dock | Off dock and **≤ 80%** — **low-battery band**; charge before overnight use. |
| LEDs change while holding in nRF Connect | Hand heat can push die temp **> 25.5°C** → **worn** mode (PPG red/IR on, battery pattern off). |

### BLE status characteristic (`3A0FF009`)

Four-byte notify (also updated on charge/worn/battery changes):

| Byte | Field |
|------|--------|
| 0 | `charging` — 0 off dock, 1 on charger (`chrsts` GPIO) |
| 1 | `worn` — 0 off-body, 1 on-body (temp > 25.5°C) |
| 2 | `device_state` — 0 not worn / not charging, 1 not worn / charging, 2 worn |
| 3 | `battery_pct` — 0–100 |

**Firmware log / config (≥ 1.0.37):**

| UUID | Role |
|------|------|
| `3A0FF00A` | Notify — UTF-8 firmware log lines (`fw: snap …`, `fw: stats …`) |
| `3A0FF00B` | Write — TLV config opcodes (see iOS `FirmwareConfigOpcode`) |
| `3A0FF00C` | Read/notify — 8-byte applied config state |

**iOS:** Settings → Developer → **Dump firmware diagnostics** (snapshot + export nRF CSV). Connect auto-enables `00A` when present.

**Firmware version:** read `3A0FF006` (string, e.g. `1.0.37-nrfconnect`).

### Worn gating & streaming

- **Off-body (`worn=0`):** PPG/ACC/temp notifies are **suppressed** after the connect probe window ends, even if CCC stays enabled.
- **Connect probe (default 10 s):** On BLE connect, firmware streams PPG/ACC/temp for `CONFIG_TGM_CONNECT_PROBE_DURATION_S` seconds even when `worn=0`, using **dim green-only** PPG LEDs so bench/charger diagnostics get real samples without locking worn from die heat. Set duration **0** in Kconfig to disable.
- **On-body cheek:** warm skin + coupling → `worn=1` → PPG/ACC streams after CCC enable (full red/IR LED profile).
- **Bench trap:** holding the clip or full-power PPG on the dock warms the MCU → can falsely set `worn=1` during long nRF Connect tests. Disconnect BLE to let the die cool below 24.5°C.

**RTT connect line (example):** `BLE link up: charging=1 worn=0 state=1 bat=54% die=22.50 C probe=10s`

### Low-voltage protection

If `CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN=y` (see root `prj.conf`), firmware **cold-reboots** when smoothed battery **< 2.8 V** for three consecutive samples (interval depends on `CONFIG_BATTERY_MEASUREMENT_INTERVAL`). Symptoms: BLE drops after ~20–30 s on a depleted cell.

**BATEN (P0.10):** boost latch must stay HIGH — `SYS_INIT` + main loop re-assert (`battery.c`, `main.c`).

### Troubleshooting quick checks

1. **Charge to solid green** on dock before judging off-dock behavior.
2. Flash + RTT: `./scripts/flash_and_rtt.sh` — look for `Battery: XXXXmV -> YY%` and `Initial charging state`.
3. nRF Connect: connect, read `3A0FF006` + `3A0FF009`, enable **battery** + **status** notifies only for a 2 min soak off dock.
4. If **Oralable** missing in scan: advertising may have stopped (`BT_LE_ADV_OPT_ONE_TIME` in `ble.c`); reconnect once or reboot; firmware fix to restart adv periodically is planned.

### Related source files

| Topic | Path |
|-------|------|
| LED logic | `app/src/battery_led_indicator.c` |
| Battery ADC | `app/src/battery.c` |
| Charge detect | `app/src/main.c` (`chrsts` GPIO) |
| Worn / state | `app/src/main.c` (`temperature_work_handler`) |
| Status notify | `app/src/tgm_service.c` (`tgm_service_send_status_notify`) |
| FW log / config GATT | `app/src/tgm_service.c` (`3A0FF00A`–`00C`) |
| Connect probe | `app/src/tgm_service.c`, `CONFIG_TGM_CONNECT_PROBE_DURATION_S` |

---

## Compatibility matrix

Record known-good pairs after `tandem_validate.sh` + manual smoke.

| Date (UTC) | Firmware | FW commit | iOS commit | FW version | Validation | Manual smoke | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 2026-05-28 | oralable_nrf | c617f81 | 547c2b7 | (pre-1.0.36 gate) | tandem_validate.sh | Build checks | PASS | Baseline battery+BLE |
| 2026-06-07 | oralable_nrf | (workspace) | (workspace) | **1.0.36-nrfconnect** | tandem + nRF Connect CSV | TGM GATT, worn gate | PASS | nRF-aligned CCC order |
| 2026-06-07 | oralable_nrf | `4210e97` | `6097a0a` | **1.0.37-nrfconnect** | build + flash smoke | 00A/B/C diagnostics, connect probe | **CURRENT** | iOS Developer dump + fw log |

### How to add a row

```bash
cd ~/work/oralable_nrf && git rev-parse --short HEAD
cd ~/work/oralable_swift && git rev-parse --short HEAD
```

*Last updated: June 2026 (1.0.37 diagnostics GATT, connect probe)*
