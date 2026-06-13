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

## Manual smoke checklist (TGM / firmware ≥ 1.0.36)

- [ ] Charger + J-Link: boots and advertises as **Oralable**.
- [ ] Battery-only: remains alive ≥ 180s (optional soak).
- [ ] **Device Manager** or **nRF Connect**: TGM `3A0FF000` + SMP services discovered.
- [ ] Read firmware `3A0FF006` → **≥ 1.0.36** (iOS `FirmwareGate` aligned).
- [ ] **Off-body:** no PPG/ACC value updates (worn gate) — expected.
- [ ] **On-body cheek:** PPG/ACC notifies after CCC enable; export nRF CSV if comparing to iOS.
- [ ] iOS app: connect → auto-record; optional CloudKit share smoke.
- [ ] OTA: `dfu_application.zip` after one-time `merged.hex` flash.

**Notes:** RTT optional for pass/fail. Primary acceptance: stable on-body BLE ≥ 2 min, correct worn gating.

---

## Compatibility matrix

Record known-good pairs after `tandem_validate.sh` + manual smoke.

| Date (UTC) | Firmware | FW commit | iOS commit | FW version | Validation | Manual smoke | Result | Notes |
|---|---|---|---|---|---|---|---|---|
| 2026-05-28 | oralable_nrf | c617f81 | 547c2b7 | (pre-1.0.36 gate) | tandem_validate.sh | Build checks | PASS | Baseline battery+BLE |
| 2026-06-07 | oralable_nrf | (workspace) | (workspace) | **1.0.36-nrfconnect** | tandem + nRF Connect CSV | TGM GATT, worn gate | **CURRENT** | nRF-aligned CCC order |

### How to add a row

```bash
cd ~/work/oralable_nrf && git rev-parse --short HEAD
cd ~/work/oralable_swift && git rev-parse --short HEAD
```

*Last updated: June 2026*
