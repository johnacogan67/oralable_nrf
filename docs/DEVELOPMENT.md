# Oralable Development Workflow (firmware + iOS)

Keeps `oralable_nrf` and `oralable_swift` synchronized. **Doc index:** [README.md](./README.md) · **Product:** [ORALABLE_MARKET_LANDSCAPE.md](./ORALABLE_MARKET_LANDSCAPE.md)

**System architecture & validation matrix:** [cursor_oralable/docs/ORALABLE_SYSTEM_ARCHITECTURE.md](../../cursor_oralable/docs/ORALABLE_SYSTEM_ARCHITECTURE.md) (truth registry, §3 status, GATT detail). **This file** covers tandem workflow, smoke checklist, and compatibility matrix only.

---

## Goals

- Develop `oralable_nrf` and `oralable_swift` in parallel.
- Validate compatibility after each meaningful change.
- Record known-good firmware/iOS commit pairs for quick rollback.
- **Lock product truth** per architecture §1 before changing `chrsts`, LED policy, or status layout.

## Source of truth

| Item | Path |
|------|------|
| Firmware | `~/work/oralable_nrf` |
| iOS | `~/work/oralable_swift` |
| System hub | `cursor_oralable/docs/ORALABLE_SYSTEM_ARCHITECTURE.md` |
| Validation script | `oralable_nrf/scripts/tandem_validate.sh` |
| Flash + RTT | `oralable_nrf/scripts/flash_and_rtt.sh` |
| OTA | [OTA_DEVICE_MANAGER.md](./OTA_DEVICE_MANAGER.md) |
| nRF Connect rule | `oralable_nrf/.cursor/rules/nrf-connect-validation.mdc` |

---

## Validation loop

1. Make changes in firmware and/or iOS.
2. Run tandem validation:

   ```bash
   cd ~/work/oralable_nrf
   ./scripts/tandem_validate.sh
   ```

3. Complete manual BLE smoke checks (below).
4. Export nRF Connect CSV for charger/off-pad if touching `chrsts` or LEDs.
5. Add a row to **Compatibility matrix** (bottom of this doc).
6. Update architecture **§3** if acceptance status changed.
7. Commit in each repo and push.

---

## Manual smoke checklist (TGM / firmware ≥ 1.0.63; **pilot ship 1.0.70**)

iOS `FirmwareGate` minimum: **1.0.63** · recommend **1.0.70**. **Ed/Pedro Phase 0 kits:** flash **1.0.70**. App **4.3.3**. Record actual `3A0FF006` after flash. See [VERSION_ALIGNMENT.md](../../cursor_oralable/docs/data_room/VERSION_ALIGNMENT.md).

**Hardware (Gen1 pilot):** BOM **REV8** · PCB **REV10** · Kaga **ES2832AA2** · charge only in the **Oralable magnetic case** (not WPC Qi). See [PRODUCT_ROADMAP.md](../../cursor_oralable/docs/PRODUCT_ROADMAP.md) · [COST_AND_TIMELINE.md](../../cursor_oralable/docs/data_room/COST_AND_TIMELINE.md) (Stage A now–2027; Stage B later).

- [ ] **Oralable magnetic charging case** + optional J-Link: boots and advertises as **Oralable** (not a wired contact dock; not a phone Qi pad).
- [ ] Battery-only: remains alive ≥ 180s (optional soak).
- [ ] **Device Manager** or **nRF Connect**: TGM `3A0FF000` + SMP services discovered.
- [ ] Read firmware `3A0FF006` (pilot: **`1.0.70`**).
- [ ] GATT includes **`3A0FF009`** (status), **`3A0FF00A`** (fw log), **`3A0FF00B`** (config write), **`3A0FF00C`** (config state) when build ≥ 1.0.37.
- [ ] **Off-body:** PPG/ACC during connect probe (disabled on Phase 0 builds) then gated; status/battery still notify.
- [ ] **On-body (Phase 0): temple** — placement mode worn; PPG/ACC notifies after CCC enable; export nRF CSV if comparing to iOS. *(Cheek / Protocol B = Phase 1+ only.)*
- [ ] **Charger path (if changed):** notify `004` + `009` only — off case 60 s → on Oralable case 60 s → off case; byte0 should toggle or use manual mode 1 (architecture §3.3 S2).
- [ ] iOS: Settings → Developer → **Dump firmware diagnostics** → export nRF-style CSV.
- [ ] iOS app: connect → auto-record; optional CloudKit share smoke.
- [ ] OTA: `dfu_application.zip` after one-time `merged.hex` flash.

**Notes:** RTT optional for pass/fail. Primary acceptance (Phase 0): stable temple BLE ≥ 2 min, honest state, HR/SpO₂ quality gates. **Do not** treat solid red on the Oralable case as proof of full SOC — case voltage can inflate ADC (architecture §3.2).

---

## LED indicators & battery (pcb00003)

Off-body status uses the **MAXM86161 PPG LEDs** (green / red channels), not a separate status LED. Implementation: `app/src/battery_led_indicator.c`.

Worn detection uses die temperature (**> 25.5°C** → worn). When worn, battery indicator LEDs stop; PPG uses red/IR for sensing.

### Target UX (FW ≥ 1.0.63 — Vitals Phase 0; energy tuning ≥ 1.0.65)

| Context | Condition | Target LED | Notes |
|---------|-----------|------------|--------|
| **On Oralable case** | STAT blinking (1.0.70) | **Flash red** | `charge_active=1` |
| **On Oralable case** | STAT taper / hold (1.0.70) | **Solid red** | Not necessarily 4.2 V |
| **Off case (bench)** | ≤ 80% | **Flash green** | 1.0.65: ~350 ms pulse / 1.2 s off |
| **Off case (bench)** | > 80% and Vmax | **Solid green** | Full off charger |

**Full threshold:** `BATTERY_LED_FULL_THRESHOLD_PCT` = **80** (`battery_led_indicator.h`).

**iOS mirror:** `DeviceStatusLEDRepresentation` in OralableCore — rendered on **VitalsDeviceStatusCard** (app **4.3.3+**).

**Percent mapping (FW ≥ 1.0.68):** CG-320B **user gauge** — 4.35 V = 100%, **3.61 V = remapped 0%** (operational empty; old chemistry curve used 3.0 V = 0%). Status byte 3 uses this map. Raw mV still on `3A0FF004`.

**Legacy (FW ≤ 1.0.62):** green flash on charger, red off case — deprecated.

### What users often see (FW **1.0.70**)

| Observation | Likely meaning |
|-------------|----------------|
| Flash **red** on Oralable case | STAT blinking — charging (`charge_active=1`) |
| **Solid red** on Oralable case | STAT taper / hold — still on pad (`charge_active=0`; not necessarily 4.2 V) |
| Flash **green** off case | Bench idle, not full |
| **Solid green** off case | Bench idle, cell at Vmax |
| LEDs change while holding in nRF Connect | Hand heat → die temp → worn (use placement mode 3 on temple) |

### BLE status characteristic (`3A0FF009`)

**FW ≥ 1.0.47:** five-byte notify/read (also updated on charge/worn/battery changes).

| Byte | Field |
|------|--------|
| 0 | `on_dock` — 0 off Oralable case, 1 on charger (`chrsts` / LTC4124 STAT; **FW ≥ 1.0.70** uses STAT blink/taper activity) |
| 1 | `worn` — 0 off-body, 1 on-body (temp + policy) |
| 2 | `device_state` — 0 off charger / 1 on charger / 2 worn |
| 3 | `battery_pct` — 0–100 (voltage estimate; inflated on pad is normal) |
| 4 | `charge_active` — **1.0.70+:** 1 while STAT is **blinking** (charging); 0 on STAT **taper** (steady assert) while still `on_dock=1`. Manual mode 1 may also set this from mV rise. |

**FW 1.0.36–1.0.46 (legacy):** four bytes (no `charge_active`; byte 0 named `charging` in older docs).

**Firmware log / config (≥ 1.0.37):**

| UUID | Role |
|------|------|
| `3A0FF00A` | Notify — UTF-8 firmware log lines (`fw: snap …`, `fw: stats …`) |
| `3A0FF00B` | Write — TLV config opcodes (see iOS `FirmwareConfigOpcode`) |
| `3A0FF00C` | Read/notify — applied config state |

**iOS:** Settings → Developer → **Dump firmware diagnostics** (snapshot + export nRF CSV). Connect uses staggered CCC per architecture §10.

**Firmware version:** read `3A0FF006` (UTF-8 string).

### Worn gating & streaming

- **Off-body (`worn=0`):** PPG/ACC hardware stopped (FIFO IRQ / ACC off); status/battery still notify.
- **Connect probe:** Disabled in vitals builds (`CONFIG_TGM_CONNECT_PROBE_DURATION_S=0`). Older FW: 10 s dim green probe when `worn=0`.
- **On-body:** warm skin + coupling or **manual mode 3** → `worn=1` → PPG/ACC streams after CCC enable (full red/IR LED profile).
- **On-body + BLE disconnect (1.0.70):** notify flags cleared, but **do not** call `tgm_service_suspend_sensor_streams` while worn — keep FIFO/ACC draining until off-body.
- **PPG INT:** MAXM86161 open-drain is **ACTIVE_LOW** — use DT `int-gpios` (`gpio_pin_configure_dt` / `gpio_pin_interrupt_configure_dt`) so `EDGE_TO_ACTIVE` is falling.
- **Advertising (Nordic NCS ≥ 3.0):** restart connectable advertising from the connection **`.recycled`** callback via `k_work` — not from `disconnected`.
- **Bench trap:** holding the clip or full-power PPG on the pad warms the MCU → can falsely set `worn=1`. Disconnect BLE to cool die below threshold.

**RTT connect line (example):** `BLE link up: charging=1 worn=0 state=1 bat=54% die=22.50 C probe=10s`

### Battery ADC policy

- Plausible cell window: **2000–5000 mV** after divider scaling.
- Implausible raw pin mV or failed scaling fallbacks: **discard** the sample (do not update `battery_value`, history, BLE notify, or charge detector).
- Do **not** clamp out-of-range candidates into 2000/5000 mV and publish — that invents a voltage.

### Soft floor (remapped 0%) and low-voltage protection

- **Soft floor (FW ≥ 1.0.68):** when cell **&lt; 3.61 V** (or remapped % &lt; 5) and not on charger placement — clear worn, stop ACC/sensing load, keep advertising + status LEDs. Does **not** cold-reboot (avoids reboot loops at mid-SOC).
- **Chemical protect:** If `CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN=y` (see root `prj.conf`), firmware **cold-reboots** when smoothed battery **&lt; 2.8 V** for three consecutive samples.

**BATEN (P0.10):** boost latch must stay HIGH — `SYS_INIT` + main loop re-assert (`battery.c`, `main.c`).

### Troubleshooting quick checks

1. Flash + RTT: `./scripts/flash_and_rtt.sh` — look for `Battery: XXXXmV -> YY%` and `Initial charging state`.
2. nRF Connect: read `3A0FF006` + `3A0FF009`, enable **battery (`004`)** + **status (`009`)** notifies for a 2 min soak off case, then on Oralable case.
3. Judge SOC and LED behavior **off the Oralable case** after a case soak (not only while on case).
4. **FW ≥ 1.0.70 CHRSTS (LTC4124 STAT) — not “broken”:** STAT **blinks while charging** and goes **steady assert** when charge current tapers (“almost full”, not necessarily 4.2 V). Firmware uses `CONFIG_CHRSTS_STAT_ACTIVITY` (edge count → `charge_active`, steady assert → on-pad taper, steady inactive → undock). Manual modes 1/2/3 still override auto. Prefer **Oralable case + USB-C** only (not MagSafe/Qi).
5. nRF Connect / RTT gate for 1.0.70: seat on Oralable case → RTT `chrsts phase` / edges and status `on_dock=1` + `charge_active=1` (flash red); leave until STAT steady → `charge_active=0` with `on_dock=1` (solid red); lift off → `on_dock=0` (green bench).
6. If **Oralable** missing in scan after disconnect: wait for connection **recycle**; `ble.c` restarts advertising from `.recycled` via `k_work` (not from the disconnect callback). Soft `ble_ensure_advertising()` only if adv is off.

### Related source files

| Topic | Path |
|-------|------|
| LED logic | `app/src/battery_led_indicator.c` |
| Battery ADC | `app/src/battery.c` |
| Charge detect | `app/src/main.c`, `app/src/charge_detector.c` |
| Worn / state | `app/src/main.c` (`temperature_work_handler`) |
| Status notify | `app/src/tgm_service.c` (`tgm_service_send_status_notify`) |
| FW log / config GATT | `app/src/tgm_service.c` (`3A0FF00A`–`00C`) |
| Connect probe | `app/src/tgm_service.c`, `CONFIG_TGM_CONNECT_PROBE_DURATION_S` |

---

## Compatibility matrix

Record known-good pairs after `tandem_validate.sh` + manual smoke. **Live validation status:** architecture doc §3.

| Date (UTC) | Firmware | FW commit | iOS commit | FW version | Validation | Manual smoke | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 2026-05-28 | oralable_nrf | c617f81 | 547c2b7 | (pre-1.0.36 gate) | tandem_validate.sh | Build checks | PASS | Baseline battery+BLE |
| 2026-06-07 | oralable_nrf | (workspace) | (workspace) | **1.0.36-nrfconnect** | tandem + nRF Connect CSV | TGM GATT, worn gate | PASS | nRF-aligned CCC order |
| 2026-06-07 | oralable_nrf | `4210e97` | `6097a0a` | **1.0.37-nrfconnect** | build + flash smoke | 00A/B/C diagnostics, connect probe | PASS | iOS Developer dump + fw log |
| 2026-06-07 | oralable_nrf | (workspace) | (workspace) | **1.0.51-nrfconnect** | nRF CSV logs 33–36 | chrsts byte0, LED policy | **PARTIAL** | See architecture §3.2–3.3; chrsts BROKEN on Oralable case |
| 2026-07-16 | oralable_nrf | (workspace) | (workspace) | **1.0.67-nrfconnect** (`app/VERSION`); kits still **1.0.66** hex | nRF Connect TBD | Bugbot + Nordic/Apple; no `CONFIG_BT_DFU_SMP`; iOS CCC timeouts | **PARTIAL** | §3b S1–S4 before tag |
| 2026-07-24 | oralable_nrf | (workspace) | (workspace) | **1.0.70** | docs stamp sync | — | — | App **4.3.3** milestone; [VERSION_ALIGNMENT](../../cursor_oralable/docs/data_room/VERSION_ALIGNMENT.md) |
| 2026-07-22 | oralable_nrf | (workspace) | (workspace) | **1.0.70** | packaged hex + docs aligned · RTT gate TBD | CHRSTS STAT activity (blink=charge) | **SHIP** | App **4.3.2**; [VERSION_ALIGNMENT](../../cursor_oralable/docs/data_room/VERSION_ALIGNMENT.md) |

### How to add a row

```bash
cd ~/work/oralable_nrf && git rev-parse --short HEAD
cd ~/work/oralable_swift && git rev-parse --short HEAD
```

*Last updated: July 2026 (pilot ship **1.0.70**; iOS **4.3.3** — [VERSION_ALIGNMENT.md](../../cursor_oralable/docs/data_room/VERSION_ALIGNMENT.md))*
