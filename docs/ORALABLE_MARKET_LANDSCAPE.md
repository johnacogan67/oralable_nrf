# Oralable Market Landscape & Product Strategy

Comprehensive positioning for Oralable MAM across hardware, mobile software, data architecture, competitive landscape, and the path from **wellness wearable** to **regulated medical device**.

**Related docs:** [Appendix A](#appendix-a-nordic-wearables-comparison) · [Appendix B](#appendix-b-ppg-sensor-comparison) · [HARDWARE_ROADMAP_nRF54L15.md](./HARDWARE_ROADMAP_nRF54L15.md) · [OTA_DEVICE_MANAGER.md](./OTA_DEVICE_MANAGER.md)

**Cross-repo:** iOS (`oralable_swift`), shared library (`OralableCore`), Python validation (`cursor_oralable`)

---

## Table of contents

1. [Executive summary](#1-executive-summary)
2. [The core insight: not a ring](#2-the-core-insight-not-a-ring)
3. [Hardware: now vs roadmap](#3-hardware-now-vs-roadmap)
4. [EMG vs PPG: second axis](#4-emg-vs-ppg-second-axis)
5. [Mobile apps & data collection](#5-mobile-apps--data-collection) — UX flows: `oralable_swift/docs/MOBILE_APP_FLOWS.md`
6. [Data architecture: wellness black box vs open pipeline](#6-data-architecture-wellness-black-box-vs-open-pipeline)
7. [Competitive map](#7-competitive-map)
8. [Regulatory spectrum: wellness → medical device](#8-regulatory-spectrum-wellness--medical-device)
9. [510(k) indication framing](#9-510k-indication-framing)
10. [Android architecture options](#10-android-architecture-options)
11. [Go-to-market: consumer vs dentist vs medical](#11-go-to-market-consumer-vs-dentist-vs-medical)
12. [Development trajectory (12–24 months)](#12-development-trajectory-1224-months)
13. [One-page summary](#13-one-page-summary)
14. [Overnight monitoring peers: SOND, Wellue, Aktiia/Hilo](#14-overnight-monitoring-peers-sond-wellue-aktiiahilo)
15. [Longitudinal ambulatory monitoring pattern](#15-longitudinal-ambulatory-monitoring-pattern)
16. [Appendix A: Nordic wearables](#appendix-a-nordic-wearables-comparison)
17. [Appendix B: PPG sensors](#appendix-b-ppg-sensor-comparison)

---

## 1. Executive summary

**Oralable is a cheek-worn Nordic BLE sensor (MAXM86161 + LIS2DTW12) aimed at sleep bruxism via hemodynamic IR-DC occlusion and jaw accelerometry — not a ring, not sEMG, not a general wellness platform.**

| Layer | Today | Trajectory |
|-------|-------|------------|
| **Hardware** | pcb00003, nRF52832, MAXM86161, 50 Hz raw BLE | nRF54L15 (Kaga ES4L15BA1), same PPG, on-device ML headroom |
| **Firmware** | TGM GATT, worn-gated streaming, MCUboot/mcumgr OTA | Richer services, in-app DFU, locked algorithm builds for clearance |
| **iOS** | Consumer + dentist apps, OralableCore, CloudKit share | Production CloudKit, feature-flag lift, clinical exports on by default |
| **Android** | Not built; marketing cites 2026 | Native Kotlin recommended; share BLE/parser spec via OralableCore port |
| **Algorithms** | Phone-side 50 Hz pipeline + Core ML Temporalis | Cross-validated vs ANR EMG; Python gold-standard in `cursor_oralable` |
| **Regulatory** | Wellness disclaimers; App Store blocks medical claims | 510(k)/MDR scaffolding in `RegulatoryPackageBuilder`; Beacon-style trials |

In the landscape, Oralable is **adjacent to EMG bruxism devices** (ANR M40, Cometa) and **orthogonal to health rings** (JCRing, Oura, IDO, WHOOP). It also parallels **longitudinal overnight monitors** — Wellue (SpO₂), Aktiia/Hilo (BP), SOND (sleep coaching) — same ambulatory + professional export shape, different biomarker (see §14–15).

---

## 2. The core insight: not a ring

Most wearables compared in this program — Oura, JCRing, IDO, Ultrahuman, WHOOP, Polar, Withings — optimize for **general wellness at a convenient vascular site** (finger or wrist). Oralable optimizes for a **specific clinical hypothesis at an unusual anatomical site**:

| Dimension | Typical health ring / strap | **Oralable MAM** |
|-----------|------------------------------|------------------|
| **Body site** | Finger or wrist (peripheral pulse) | **Cheek / masseter** (jaw muscle occlusion) |
| **Primary signal** | PPG for HR, HRV, SpO₂, sleep staging | **PPG + IR-DC hemodynamic occlusion** + accelerometer jaw vibration |
| **Primary use case** | Readiness, strain, general sleep quality | **Sleep bruxism** (clenching/grinding detection) |
| **Signal modality** | Optical only (sometimes + skin temp, ECG) | Optical + motion; **not sEMG** |
| **Algorithm home** | Mostly on-device or black-box cloud | **50 Hz raw stream → phone-side pipeline** (Swift + Python) |

```
                    General wellness          Condition-specific
                           │                         │
    Wrist/finger site ─────┼──── Rings, WHOOP, Oura   │
                           │                         │
                           │              ScanWatch, Polar
                           │                         │
    Jaw/cheek site ────────┼─────────────────────────┼── Oralable (PPG/IR-DC)
                           │                         │
                           │              ANR M40, Cometa (EMG)
```

Oralable sits in a **niche orthogonal to rings**: same broad sensor classes (PPG, ACC), but a different physics problem (muscle occlusion on the masseter vs finger perfusion). That is closer to **dental / sleep medicine** than fitness — even though the hardware BOM looks superficially similar to a small Nordic health device.

---

## 3. Hardware: now vs roadmap

### 3.1 Today (shipping path: pcb00003)

| Block | Choice | Implication |
|-------|--------|-------------|
| MCU | **nRF52832** (512K Flash, 64K RAM) | Same tier as ArcX, early Polar — smallest/cheapest Nordic health tier |
| PPG | **ADI MAXM86161** @ I²C 0x62 | Only product in the competitive set with a **public, named** integrated PPG module |
| ACC | **LIS2DTW12** @ 50 Hz | Jaw vibration / actigraphy, sync-tap anchoring |
| Battery | CG-320B 15 mAh LiPo | Small cell; multi-day life TBD; 8 h clinical night is design target |
| BLE | Custom **TGM GATT** service (`3A0FF000`) | Raw 50 Hz PPG (R/G/IR) + ACC + temp + status |
| OTA | **MCUboot + mcumgr SMP** | Nordic Device Manager on iPhone; open NCS stack |
| FW version | **1.0.37-nrfconnect** (diagnostics GATT `00A`–`00C`; iOS minimum **1.0.36** via `FirmwareGate`) |

**BLE characteristics (TGM service):**

| UUID suffix | Role | Payload |
|-------------|------|---------|
| `...f001` | PPG notify | Frame counter + R/G/IR UInt32 samples |
| `...f002` | ACC notify | Frame counter + X/Y/Z Int16 |
| `...f003` | Temp notify | Frame counter + centi-°C |
| `...f004` | Battery | mV, read + notify |
| `...f005` | Device ID | uint64 |
| `...f006` | Firmware version | string |
| `...f009` | Status | charging, **worn**, device_state, battery % |

**Streaming policy:** On-body → full PPG/ACC at 50 Hz; off-body → PPG/ACC hardware stopped, status still available. Worn detection uses die temperature thresholds (~25.5 °C on / ~24.5 °C off), with firmware status char as primary source for iOS.

### 3.2 Roadmap (data room: nRF54L15 generation)

| Block | Change | Why it matters |
|-------|--------|----------------|
| MCU | **Kaga ES4L15BA1** → **nRF54L15** (1.5 MB Flash, 256 KB RAM, BLE 6.0) | Moves to **IDO IDR01 tier** — room for on-device bruxism ML |
| PPG | **MAXM86161EFD+** (same silicon) | Deliberate choice *not* to chase Oura 4 / WHOOP 5 discrete **MAX86171/86178** stacks |
| Form | Still cheek clip | Optical path tuned for masseter, not finger vascular bed |

See [HARDWARE_ROADMAP_nRF54L15.md](./HARDWARE_ROADMAP_nRF54L15.md).

### 3.3 Nordic MCU tier map

```
Smallest/cheapest     nRF52832  →  Oralable (now), ArcX, early Polar
Health ring sweet spot nRF52840 →  JCRing, Ultrahuman, Withings, WHOOP 4
More compute          nRF5340  →  Polar Loop, GECA 2.0
Next-gen rings        nRF54L15 →  IDO IDR01, Oralable (planned)
Non-Nordic anchors    PSoC 6 (Oura), Ambiq (WHOOP 5), Samsung SIP (Galaxy Ring)
```

Oralable is **below** JCRing/WHOOP on MCU resources today; nRF54L15 closes that gap without changing the PPG part.

---

## 4. EMG vs PPG: second axis

Bruxism has historically been measured with **surface EMG** on the masseter. The Oralable ecosystem acknowledges this explicitly.

| Approach | Examples | Signal | Oralable relationship |
|----------|----------|--------|-------------------------|
| **sEMG** | Neeno 2, Myo, uMyo, IOMICO, Cometa (clinical) | Electrical µV at skin | **ANR M40** in iOS as **comparison/research** path |
| **PPG / IR-DC** | Oralable (consumer cheek clip) | Hemodynamic occlusion + jaw motion | Primary product signal |

**iOS parallel BLE device types:**

- **Oralable REV10** → PPG R/G/IR, ACC, temp, firmware worn status (TGM `3A0FF000`)
- **ANR M40 Muscle Sense** → EMG analog (`2A58`), Automation I/O service `1815`

`RecordingSession` and `DeviceManagerFactory` distinguish EMG vs IR sessions. This is a **validation architecture**: PPG-based bruxism vs clinical gold standard (EMG), without shipping electrodes to consumers.

See [Appendix A](#appendix-a-nordic-wearables-comparison) (EMG section).

---

## 5. Mobile apps & data collection

### 5.1 iOS ecosystem (exists today)

Two App Store targets share **OralableCore** (`OralableCore/Sources/OralableCore/`):

| App | Bundle | Audience | Role |
|-----|--------|----------|------|
| **OralableApp** | `com.jacdental.oralable` | Consumer / patient | Pair device, auto-record, dashboard, share with clinician |
| **OralableForProfessionals** | `com.jacdental.oralable.dentist` | Dentists / clinicians | Participants, historical analytics, CloudKit + CSV import |

**Product lines in consumer app** (`DeviceManagerFactory`):

- **Temporalis Headband** = Oralable REV10 (`.oralable`, 50 Hz)
- **ANR Muscle Sense** = ANR M40 (`.anr`, EMG research)
- **Intraoral band** = placeholder, not pairable yet

### 5.2 BLE → app data path (Oralable device)

```
Oralable REV10 (BLE)
  → BLECentralManager
    → DeviceManager / DeviceConnectionCoordinator
      → OralableDevice (TGM GATT parse)
        → DeviceManagerAdapter (50 Hz alignment)
          ├→ SensorDataProcessor (history, auto-flush CSV)
          ├→ UnifiedBiometricProcessor (HR, SpO₂, Temporalis/TFI)
          ├→ AutomaticRecordingSession (state events, pause/resume on disconnect)
          └→ SessionHistoryStore (hourly TFI, SASHB rollups)
```

**Connect sequence** (nRF Connect–aligned):

1. Read device ID (`3A0FF005`)
2. Read firmware version (`3A0FF006`) → **FirmwareGate** (minimum **1.0.36**)
3. Staggered CCC enables (battery → status → PPG → ACC → temp)
4. Worn-gated streaming; `ConnectionReadiness.ready`
5. Start `AutomaticRecordingSession`

**Key source files:**

| Area | Path |
|------|------|
| BLE orchestration | `oralable_swift/.../Managers/DeviceManager.swift` |
| Connect flow | `oralable_swift/.../Managers/DeviceConnectionCoordinator.swift` |
| Oralable device | `oralable_swift/.../Devices/OralableDevice/` |
| ANR device | `oralable_swift/.../Devices/ANRMuscleSenseDevice.swift` |
| BLE parsing | `OralableCore/.../BLE/BLEDataParser.swift` |
| Firmware gate | `oralable_swift/.../Utilities/FirmwareGate.swift` |
| Auto recording | `OralableCore/.../Events/AutomaticRecordingSession.swift` |

### 5.3 Data types

| Layer | Data |
|-------|------|
| **Raw BLE** | PPG R/G/IR (UInt32), ACC X/Y/Z (Int16), temp (centi-°C), battery mV, status (worn/charging/device_state/battery%) |
| **Derived (app)** | Heart rate, SpO₂, muscle activity (IR-DC path), Temporalis probabilities, TFI, SASHB |
| **Session metadata** | State transition events, hourly rollups, EMG vs IR `deviceType` |
| **Export** | 50 Hz research CSV, clinical PDF, professional handshake JSON, CloudKit compressed daily blobs |

### 5.4 CloudKit & sharing

- **Container:** `iCloud.com.jacdental.oralable.shared`
- **Record types:** `ShareInvitation`, `SharedPatientData`, `HealthDataRecord`
- **Consumer:** `SharedDataManager` — 6-digit share codes, daily LZFSE-compressed sensor JSON
- **Professional:** `ProfessionalDataManager` — query by share code + CSV import
- **Handshake:** `ProfessionalHandshakeExport` — hourly TFI/SASHB rollups for dentist review

Production CloudKit schema deployment remains on launch checklist (`LAUNCH_READINESS_CHECKLIST.md`).

### 5.5 Export paths

| Path | Component | Format |
|------|-----------|--------|
| Share screen | `ShareView` | Events vs all samples CSV |
| Research raw | `ResearchRawDataExport` | 50 Hz PPG/ACC/temp/HR/SpO₂ CSV |
| Clinical PDF | `ClinicalReportGenerator` | TFI, SASHB, smoking-gun correlation |
| Log merge | `LogExportManager` | CSV/JSON |
| Auto flush | `AutoFlushService` | Hourly tmp CSV during long sessions |
| nRF debug | `NRFConnectBLELogger` | nRF Connect CSV format |
| Professional import | `CSVParser` (dentist app) | Participant CSV |

### 5.6 Android (not built)

No Kotlin/Android project exists in the repos. Marketing (`oralable-website/product.html`, `about.html`) cites **Android coming 2026**.

See [§10 Android architecture options](#10-android-architecture-options).

---

## 6. Data architecture: wellness black box vs open pipeline

### 6.1 Typical ring/strap (JCRing, Oura, WHOOP)

```
Sensors on device
  → On-device or companion-chip algorithms
    → BLE: mostly scores (HR, HRV, sleep stage, strain, SpO₂ %)
      → Vendor app + cloud
        → User sees readiness / sleep score
```

Raw PPG is often **not** exposed. Algorithms are proprietary.

### 6.2 Oralable (current architecture)

```
MAXM86161 + LIS2DTW12 on nRF52832
  → 50 Hz raw PPG + ACC over BLE (TGM notify)
    → iOS OralableCore + Python (cursor_oralable)
      → Bruxism detection (IR-DC trough depth, ACC sync taps, 50 Hz resample)
        → Consumer dashboard + clinical exports + optional CloudKit to dentist
```

**Parallel Python research stack** (`cursor_oralable`):

- Gold-standard validation CSV
- Clinical reports (TFI, SASHB, SpO₂–clench timing)
- Protocol segments (sync-tap anchoring, tonic/phasic/apnea phases)
- Algorithm development feeding OralableCore (`MAMInferenceManager`, Core ML `BruxismMAM_Temporalis`)

This is closer to a **research instrument with a consumer shell** than a wellness ring — a strength for validation and a regulatory liability if marketed incorrectly.

### 6.3 Processing rules (project standard)

- All final time series resampled to **50 Hz** (20 ms) via linear interpolation
- PPG: Butterworth bandpass 0.5–8.0 Hz (HR); low-pass &lt;1 Hz for IR-DC muscle occlusion
- Accelerometer: actigraphy + jaw vibration; sync with 50 Hz PPG
- Clench detection cross-verified against **IR-DC trough depth**
- Default cheek coupling: **R_G_IR**; IR-DC raw range **10M–70M** (32-bit firmware)

---

## 7. Competitive map

### 7.1 Closest peers (same problem, different solution)

| Product | Why adjacent | Why different |
|---------|--------------|---------------|
| **ANR M40 / Cometa EMG** | Same anatomy, bruxism question | Electrical vs hemodynamic |
| **Night guards + apps** | Dental bruxism ecosystem | No continuous sensing |
| **Research cheek PPG** | Same site hypothesis | Not commercial |

### 7.2 Same hardware tier, different problem

| Product | Shared DNA | Divergence |
|---------|------------|------------|
| **JCRing** | nRF + PPG + sleep | Finger site, OEM wellness, undisclosed PPG |
| **IDO IDR01** | nRF54L15 roadmap peer | Ring form, general health |
| **Ultrahuman** | Nordic BLE health | Metabolic/fitness, STM32G0 sensor MCU |
| **ArcX** | nRF52832 | No health stack |

See [Appendix B](#appendix-b-ppg-sensor-comparison) for JCRing vs Oralable PPG detail.

### 7.3 Aspirational tier

| Product | What they have that Oralable lacks (today) |
|---------|---------------------------------------------|
| **Oura 4** | Multi-PD MAX86178, PSoC 6 compute, brand, published sleep science |
| **WHOOP 5** | Ambiq + 14-day battery, strain ecosystem, ECG tier |
| **Withings ScanWatch** | JMIR-validated PPG, ECG, hybrid watch form |
| **Polar Elixir** | Precision Prime 9-LED, sport HR dominance |

Oralable does not need to beat them on readiness scores. It needs to **own jaw occlusion sensing** better than any ring could from a finger.

### 7.4 Side-by-side snapshot

| | **Oralable** | **JCRing X3** | **Oura Gen 4** | **WHOOP 5** | **ANR M40** |
|---|:---:|:---:|:---:|:---:|:---:|
| Form | Cheek clip | Finger ring | Finger ring | Wrist strap | EMG pod |
| Wireless MCU | nRF52832 → nRF54L15 | nRF52840 | PSoC 6 | Ambiq | Unpublished |
| PPG / signal | MAXM86161 R/G/IR | Undisclosed multi-λ | MAX86178 | MAX86171 | sEMG |
| Primary use | Sleep bruxism | General wellness | Sleep & readiness | Strain/recovery | Muscle activation |
| Raw stream to phone | **50 Hz PPG+ACC** | B2B SDK (partial) | No | No | EMG level |
| OTA | MCUboot/mcumgr | Vendor | Vendor | Vendor | Vendor |
| Regulatory (public) | Wellness | OEM "medical cert" marketing | Wellness | Wellness + ECG tier | Research/clinical |

### 7.5 SOND Dreambuds (sleep earbuds — Kickstarter 2026)

Boston startup (CEO: former Bose Head of Global Sleep). **Dreambuds** = in-ear sleep earbuds with **closed-loop AI audio coaching**, not passive tracking.

| Item | Detail |
|------|--------|
| **Form** | In-ear buds + **smart case** (Wi‑Fi, OLED, speaker, phone-free overnight) |
| **Signals** | ~12 physiological: respiration, HR, HRV, sleep staging, body position, snoring, **SCG** (seismocardiography), motion, mics |
| **PPG** | **No** — markets **biomechanical sensors, not optical PPG** |
| **Intervention** | Cloud AI adjusts audio in real time (“respond at 2 AM”) |
| **Bruxism** | **Not marketed** |
| **Kickstarter** | May 2026; ~$449 early / $650 retail; ship target Q2 2026 |
| **Nordic nRF** | **Not disclosed** — no Nordic press release; likely **audio SoC** (Qualcomm-class) not nRF sensor MCU |
| **Peer** | Ozlo Sleepbuds (ex-Bose lineage); SOND claims real-time closed loop vs Ozlo passive |

**Relevance to Oralable:** Same **overnight sleep** moment and premium hardware price band; **orthogonal** on anatomy (ear vs cheek) and signal (biomechanical/audio vs IR-DC jaw occlusion). Not a Nordic firmware peer. See conversation notes in §14.

### 7.6 Wellue / Viatom O2Ring (finger SpO₂ ring)

**Clinical-style finger pulse oximeter** in ring form (Viatom / Wellue). **FDA-cleared** Smart Ring Pulse Oximeter (2025).

| Item | Detail |
|------|--------|
| **Form** | Finger ring (thumb/index) |
| **PPG** | **Transmissive** — Red 660 nm + IR 940 nm through finger (hospital fingertip logic) |
| **Measures** | SpO₂, pulse rate, **motion** — not bruxism, not jaw |
| **Rate** | SpO₂ ~1 Hz to user; app charts ~4 s; O2Ring-S claims 200 Hz internal sampling |
| **Alerts** | Vibration on low SpO₂ / abnormal HR (CPAP, positional apnea adjunct) |
| **BLE** | Proprietary — legacy GATT `14839ac4…` or **OxyII** on O2Ring-S (encrypted) |
| **Nordic nRF** | Common in Viatom line; **not** marketed like JCRing/WHOOP Nordic PRs |
| **Raw stream** | Processed SpO₂/HR only — not open 50 Hz PPG |

**Overlap with Oralable:** Overnight wear, **SpO₂ / hypoxic burden** (Oralable **SASHB**), HR, motion, PDF export to clinician. **Wellue wins** on oximeter credibility; **Oralable wins** on masseter IR-DC and dental workflow. **Complementary** in dental sleep medicine (oxygen + jaw), not substitutable.

### 7.7 Aktiia / Hilo (cuffless optical BP bracelet)

Swiss **Aktiia** → consumer brand **Hilo** (EU); **G0** system in US. **First FDA 510(k) OTC cuffless BP monitor** (July 2025).

| Item | Detail |
|------|--------|
| **Form** | Wrist bracelet pod |
| **PPG** | **Green 526 nm** reflective — pulse wave analysis, **not** multi-wavelength SpO₂ |
| **Output** | Systolic/diastolic BP (mmHg), HR — ~2 readings/hour when **still** |
| **Calibration** | **Oscillometric cuff** included; init + ~monthly re-cal |
| **Algorithm** | **Cloud** OBPM / PWA on large optical dataset |
| **Regulatory** | **CE Class IIa** + **FDA 510(k) OTC** — cleared **medical device** |
| **BLE** | BLE 5.0 to app; not open raw PPG stream |
| **Nordic nRF** | **Not documented** |
| **Indication** | Hypertension / cardiovascular monitoring — **not** bruxism or sleep |

**Relevance to Oralable:** **Regulatory and product-architecture benchmark** (optical PPG → cleared monitoring claim), not a market competitor. Aktiia proves pulse **morphology** from PPG can support clearance — analogous science class to Oralable’s **IR-DC morphology**, different endpoint (BP vs jaw occlusion). See §15.

### 7.8 Extended peer snapshot (overnight + longitudinal)

| | **Oralable** | **Wellue O2Ring** | **Aktiia / Hilo** | **SOND Dreambuds** |
|---|:---:|:---:|:---:|:---:|
| **Primary metric** | TFI, SASHB, jaw events | SpO₂, PR | BP (mmHg) | Sleep coaching + staging |
| **Site** | Cheek / masseter | Finger | Wrist | Ear canal |
| **PPG type** | Reflective R/G/IR | Transmissive R+IR | Reflective green | **None** (biomechanical) |
| **Raw to phone** | **50 Hz open GATT** | ~1 Hz SpO₂ % | Sparse BP + cloud | Small BLE packets to case |
| **Pro channel** | Dentist app + handshake | PDF for doctor | HCP dashboard | Consumer only |
| **Cleared today?** | Wellness | **FDA oximeter** | **FDA + CE BP** | Wellness |
| **nRF documented?** | **Yes** (nRF52832) | Unlikely headline | No | **No** |

---

## 8. Regulatory spectrum: wellness → medical device

### 8.1 Current public positioning (wellness)

Across website, support pages, onboarding, and App Store compliance tests:

- **"Wellness device, not a medical device"**
- Does not diagnose, treat, cure, or prevent disease
- Monitors muscle activity for **personal awareness**
- Metadata tests **block** strings containing "FDA", "medical device", "diagnose", "cure"

Same legal posture as most consumer rings — with the caveat that **bruxism** is clinically meaningful, so the line is thinner than "steps counted."

### 8.2 Internal regulatory infrastructure (exists, not shipped as product claims)

| Capability | Location | Purpose |
|------------|----------|---------|
| `RegulatoryPackageBuilder` | `oralable_swift/.../Managers/Regulatory/` | FDA **510(k)** and **CE Mark (EU MDR)** package generation |
| `RegulatoryModels` | `oralable_swift/.../Models/Regulatory/` | ISO 14971 risk severities, compliance reports |
| Pilot study tooling | `PilotDataManager`, `Anonymizer` | Consent-gated batch export, de-identification |
| Clinical report generator | `ClinicalReportGenerator` | TFI, SASHB, SpO₂–clench correlation PDF |
| Professional handshake | `ProfessionalHandshakeExport` | Hourly clinical rollups for dentist review |
| Python validation | `cursor_oralable/docs/` | Gold-standard protocol, clinical evaluation reports |
| Beacon Hospital trials | Website / docs mentions | External clinical validation (research, not clearance) |

### 8.3 Stages of the trajectory

```
┌─────────────────────────────────────────────────────────────────┐
│ TODAY — Wellness                                                │
│ Consumer iOS · wellness disclaimers · personal bruxism awareness│
│ Optional share with dentist                                     │
└────────────────────────────┬────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│ NEAR TERM — Clinical evidence                                   │
│ Beacon / pilot studies · ANR EMG cross-validation              │
│ Clinical PDF + handshake exports · Python gold-standard         │
└────────────────────────────┬────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│ MID TERM — SaMD / device clearance                              │
│ 510(k) or De Novo · EU MDR CE · IEC 62304 · ISO 14971           │
│ Locked intended use: nocturnal bruxism monitoring               │
└────────────────────────────┬────────────────────────────────────┘
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│ LONGER TERM — Medical tier                                      │
│ Diagnostic claims with cleared algorithms                       │
│ EHR integration / HIPAA BAA · Rx or HCP-only distribution     │
│ Locked on-device algorithms vs open research mode               │
└─────────────────────────────────────────────────────────────────┘
```

| Stage | Product class | Claims allowed | Data handling |
|-------|---------------|----------------|---------------|
| **Wellness (now)** | General wellness wearable | "Awareness", "trends", "share with your dentist" | User-owned; CloudKit opt-in |
| **Clinical investigation** | Study device under protocol | Research endpoints only | Anonymized pilot export; IRB |
| **SaMD / cleared device** | Medical device (US) / MDR Class IIa/b (EU) | Specific indication (e.g. nocturnal bruxism monitoring) | Design controls, validation dossier, PMS |
| **Professional medical** | May require Rx or HCP channel | Treatment planning support | HIPAA, audit trails, locked SW versions |

### 8.4 How competitors play regulatory

| Product | Regulatory posture (public) |
|---------|----------------------------|
| **Oura, WHOOP, JCRing** | Wellness; general health; some clinical partnerships |
| **JCRing** | Some SKUs marketed with "medical certificate" (OEM-dependent — not FDA clearance) |
| **Withings ScanWatch** | AFib detection has regulatory paths in some regions |
| **WHOOP MG** | ECG tier moves toward medical-adjacent |
| **Cometa / clinical EMG** | True medical/research tier from day one |
| **Wellue O2Ring** | **FDA-cleared pulse oximeter** (2025); home/clinical SpO₂ collection |
| **Aktiia / Hilo** | **CE Class IIa** + **FDA 510(k) OTC** cuffless BP (2025) — reference clearance path for optical wearables |
| **SOND Dreambuds** | Wellness; “medical-grade accuracy” marketing |
| **Oralable** | Wellness today; **bruxism-specific** indication is the natural clearance target |

---

## 9. 510(k) indication framing

*This section is strategic framing for regulatory planning, not legal advice. Engage a regulatory consultant before submission.*

### 9.1 Why 510(k) fits (vs De Novo)

Oralable's natural clearance path is **510(k) substantial equivalence** to existing **bruxism / sleep monitoring** predicates — not a De Novo for a wholly new device type. The novelty is **optical IR-DC at the masseter**, not the clinical endpoint (nocturnal bruxism activity).

**Candidate predicate families** (to be confirmed with regulatory counsel):

| Predicate class | Examples | Equivalence argument |
|-----------------|----------|----------------------|
| **Home sleep / bruxism monitors** | FDA-cleared bruxism or sleep activity devices (EMG or combined sensor) | Same **intended use**: monitor nocturnal jaw muscle activity during sleep |
| **PPG vitals wearables** (weaker predicate) | Pulse oximeters, HR monitors | Same sensor modality (PPG) but **different site and algorithm** — likely insufficient alone |

The primary story: **same intended use and user population** (adults with suspected sleep bruxism), **different technological characteristics** (hemodynamic occlusion vs sEMG), with **non-inferiority or superiority** demonstrated in clinical validation.

### 9.2 Proposed intended use statement (draft)

> The Oralable Oral Activity Monitor is intended for use by adults in the home environment to **monitor and record episodes of nocturnal jaw muscle activity consistent with sleep bruxism** (clenching and grinding). The device is worn on the cheek over the masseter muscle. Data are transmitted to a mobile application for review by the user and, optionally, their dental care provider. The device is **not intended** to diagnose sleep disorders, replace polysomnography, or guide acute treatment decisions without professional interpretation.

Tune language with counsel to avoid overlapping with **PSG** or **OSA diagnostic** claims unless separately validated.

### 9.3 Device description (regulatory)

| Element | Description |
|---------|-------------|
| **Device name** | Oralable Oral Activity Monitor (MAM) |
| **Hardware** | Cheek-worn clip; nRF52/nRF54 BLE MCU; MAXM86161 PPG (R/G/IR); LIS2DTW12 accelerometer; rechargeable LiPo |
| **Software** | Mobile application (iOS; Android when available) — BLE pairing, 50 Hz signal processing, event detection, reporting |
| **SaMD boundary** | Bruxism event detection algorithm + reporting UI are likely **Software as a Medical Device** if sold with diagnostic/monitoring claims |
| **Accessories** | Charging dock; optional professional viewer app |

Align with `RegulatoryPackageBuilder.Configuration` defaults: device name **"Oralable Oral Activity Monitor"**, manufacturer **JAC Dental Ltd**.

### 9.4 Substantial equivalence narrative (technological characteristics)

| Characteristic | Predicate (typical EMG bruxism monitor) | Oralable (new) | Performance data needed |
|----------------|-------------------------------------------|----------------|-------------------------|
| **Sensor** | Surface EMG electrodes | PPG + IR-DC + accelerometer | Concordance study vs EMG (ANR M40 / Cometa) |
| **Site** | Masseter / temporalis | Masseter (cheek clip) | Placement reproducibility |
| **Output** | EMG burst events, duty cycle | IR-DC occlusion events, ACC jitter, TFI | Sensitivity/specificity vs gold standard |
| **Environment** | Home sleep | Home sleep | Same |
| **User** | Adult bruxism suspect | Adult bruxism suspect | Same |

**Key validation already instrumented in codebase:**

- ANR M40 parallel recording in iOS
- Python protocol validation (`cursor_oralable` — sync taps, tonic/phasic/apnea phases, false-positive gates for swallow/speech)
- `ClinicalReportGenerator` smoking-gun correlation (TFI, SASHB)
- `RegulatoryPackageBuilder` for aggregating validation results and ISO 14971 risks

### 9.5 Software lifecycle (IEC 62304)

| Software item | Safety class (typical) | Notes |
|---------------|------------------------|-------|
| Firmware (BLE stream, worn gate) | A or B | No direct diagnosis on device today |
| Mobile app — event detection | **B or C** | Drives clinical output if cleared |
| CloudKit sync | B | Data integrity, not primary algorithm |
| Professional viewer | B | Displays derived clinical metrics |

Plan for **frozen algorithm builds** at submission; separate "research mode" (raw export) from "cleared mode" (locked thresholds and Core ML model versions).

### 9.6 Clinical validation package (minimum credible)

1. **Concordance study:** N ≥ 30 (pilot) / N ≥ 100 (submission-grade) nights, Oralable vs simultaneous PSG + EMG or ANR reference
2. **Endpoints:**
   - Event detection sensitivity / specificity vs EMG burst criteria
   - False positive rate during swallow, speech, head movement (already in protocol docs)
   - IR-DC coupling stability across skin types and coupling range 10M–70M
3. **Usability:** IEC 62366 — cheek placement, worn detection, overnight disconnect handling
4. **Bench:** BLE reliability, battery 8 h clinical night, EMC (IEC 60601-1-2 if medical electrical equipment classification applies)

Populate `RegulatoryPackageBuilder` validation results from these studies as they complete.

### 9.7 EU MDR parallel

- Likely **Class IIa** (rule-dependent) for non-invasive monitoring of physiological parameters
- **Clinical evaluation report (CER)** referencing equivalence or clinical investigation
- **Post-market surveillance** plan — CloudKit aggregate stats (privacy-preserving) could support PMS if designed correctly

`RegulatoryScope` in code already supports `FDA_510K`, `CE_MARK`, or both.

### 9.8 What must change before filing

| Today (blocks filing) | Required for 510(k) |
|-----------------------|---------------------|
| Wellness disclaimers everywhere | Intended use aligned with monitoring claim |
| Open research exports default | Validated, locked algorithm version |
| Feature flags hide clinical cards | Clinical metrics part of cleared labeling |
| No published multi-site study | Clinical validation report in dossier |
| OTA via Nordic Device Manager only | Validated SW update process (IEC 62304) |
| Android not shipped | Define platform scope in submission (iOS-only acceptable initially) |

---

## 10. Android architecture options

Marketing commits to **Android in 2026** (`oralable-website/product.html`). No Android code exists. Below are three viable architectures with a recommendation.

### 10.1 Requirements (parity with iOS)

Any Android implementation must reproduce:

| Capability | Spec source |
|------------|-------------|
| TGM GATT connect sequence | `DeviceConnectionCoordinator`, `BLEConstants.TGM` |
| Firmware gate ≥ 1.0.36 | `FirmwareGate.swift` |
| PPG/ACC/temp/status parsing | `BLEDataParser.swift` |
| 50 Hz alignment / bucketing | `DeviceManagerAdapter` |
| Worn-gated recording | `AutomaticRecordingSession`, `TGMDeviceStatus` |
| Background BLE + reconnect | `BLEBackgroundWorker` patterns |
| Export CSV (50 Hz research) | `ResearchRawDataExport`, `CSVExporter` |
| CloudKit equivalent **or** cross-platform backend | See §10.5 |

**Out of scope for v1 Android (acceptable gaps):**

- ANR M40 (iOS research path) — defer unless ANR ships Android SDK
- HealthKit — use Health Connect on Android
- Core ML Temporalis — port to TFLite or run simplified on-device model
- Sign in with Apple — Google Sign-In equivalent

### 10.2 Option A: Native Kotlin + Android BLE (recommended)

```
┌─────────────────────────────────────────────────────────────┐
│  oralable_android/                                          │
│  ├─ app/              (OralableApp + Professionals flavors) │
│  ├─ core-ble/         TGM parser, GATT client               │
│  ├─ core-algorithms/  50 Hz pipeline (Kotlin port)          │
│  ├─ core-data/        Room DB, recording sessions           │
│  └─ core-export/      CSV, handshake JSON                   │
└─────────────────────────────────────────────────────────────┘
         │ BLE                     │ optional
         ▼                         ▼
   Oralable REV10            Firebase / custom API
                             (if not CloudKit on Android)
```

**Pros:**

- Best BLE background behavior on Android (foreground service, `BluetoothLeScanner`)
- Matches existing Swift architecture — port `OralableCore` module-by-module
- Play Store + enterprise distribution for dentist app
- Nordic Device Manager already on Android for OTA

**Cons:**

- Two codebases to maintain (Swift + Kotlin)
- Algorithm parity requires disciplined shared test vectors

**Implementation order:**

1. `core-ble`: GATT client + `BLEDataParser` port with unit tests from recorded nRF Connect CSVs
2. `core-algorithms`: 50 Hz resample, Butterworth, IR-DC — validate against Python gold CSVs in `cursor_oralable`
3. Consumer app MVP: scan, connect, dashboard, record, export CSV
4. Cloud sync layer (see §10.5)
5. Professional flavor

**Effort estimate:** 4–6 months for MVP parity with iOS consumer path (1–2 engineers familiar with BLE).

### 10.3 Option B: Kotlin Multiplatform (KMM)

Share business logic between iOS and Android; keep UI native (SwiftUI + Compose).

```
┌──────────────────┐     ┌──────────────────┐
│  iOS SwiftUI     │     │  Android Compose │
└────────┬─────────┘     └────────┬─────────┘
         │                        │
         └──────────┬─────────────┘
                    ▼
         ┌──────────────────────┐
         │  shared KMM module   │
         │  BLE parse, 50Hz,    │
         │  CSV, session state  │
         └──────────────────────┘
```

**Pros:** Single source of truth for algorithms and parsers; fewer drift bugs

**Cons:** Significant refactor of existing `OralableCore` Swift; Core ML integration stays iOS-only; team Kotlin/Swift fluency required

**Verdict:** Strong for **year 2** if team grows; poor fit for **2026 MVP** given mature Swift codebase.

### 10.4 Option C: Flutter (or React Native)

Single UI codebase; platform channels for BLE.

**Pros:** One UI team, faster visual parity

**Cons:**

- BLE background on Android is painful in cross-platform frameworks
- IOMICO EMG tracker uses Flutter — known pattern, but Oralable's 50 Hz stream + reconnect is heavier than chart-only EMG
- Would still rewrite algorithms in Dart or via FFI

**Verdict:** Not recommended for primary path given BLE complexity and existing native iOS investment.

### 10.5 Cloud / sharing on Android

CloudKit is **Apple-only**. Android dentist sharing requires one of:

| Approach | Description |
|----------|-------------|
| **Firebase / Firestore** | Cross-platform share codes; mirror `HealthDataRecord` schema |
| **Custom REST API** | JAC Dental backend; HIPAA BAA if medical tier |
| **Export-only v1** | Android consumer exports CSV; dentist imports manually (already supported in professional app) |

**Pragmatic 2026 plan:**

- **Phase 1:** Android consumer with local recording + CSV export + manual share
- **Phase 2:** Firebase sync compatible with iOS CloudKit schema (or unified backend migration for both platforms)

### 10.6 OTA on Android

Firmware OTA stays **out of consumer app v1** — use **Nordic Device Manager for Android** (same mcumgr SMP as iOS). In-app DFU is a later product polish item.

### 10.7 Recommendation summary

| Priority | Choice |
|----------|--------|
| **2026 MVP** | **Option A — Native Kotlin**, modular `core-ble` + `core-algorithms` |
| **Test strategy** | Golden CSVs from `NRFConnectBLELogger` + Python validation outputs |
| **Sync** | CSV export first; Firebase in phase 2 |
| **Avoid for now** | Flutter BLE, full KMM rewrite |

---

## 11. Go-to-market: consumer vs dentist vs medical

Three commercial paths map to regulatory stages. The codebase already supports all three structurally; **marketing and claims** must match the chosen path.

### 11.1 Path A: Wellness consumer (current default)

**Buyer:** Individual with bruxism awareness, jaw pain, or dentist recommendation

**Product:**

- Oralable cheek clip + **Oralable** iOS app
- Subscription (StoreKit 2 — 6 products configured)
- Optional 6-digit share to dentist

**Messaging (App Store compliant):**

- "Monitor nighttime grinding patterns"
- "Share trends with your dental care provider"
- **Not** "diagnose bruxism" or "FDA-cleared"

**Channels:**

- Direct-to-consumer (website, App Store)
- Dentist referral (leaflet with share-code setup)
- Amazon / dental retail (hardware)

**Revenue model:**

- Hardware margin + app subscription
- Freemium: basic recording free, advanced analytics / export / share behind paywall (per IAP setup)

**Feature flags today:** Most dashboard cards hidden pre-launch (`FeatureFlags.swift`); pilot off; CloudKit share off by default.

**Launch blockers (from checklist):** CloudKit production schema, App Store Connect IAP live.

### 11.2 Path B: Professional / dentist channel (near term)

**Buyer:** Dental practice monitoring bruxism patients remotely

**Product:**

- Patient uses **Oralable** app + device
- Dentist uses **Oralable for Dentists** (`com.jacdental.oralable.dentist`)
- CloudKit or CSV handshake with hourly TFI / SASHB rollups

**Messaging (still wellness-adjacent unless cleared):**

- "Remote bruxism pattern monitoring for your patients"
- "Data-driven splint and treatment conversations"
- App Store metadata uses "HIPAA-conscious" — not a BAA by itself

**Channels:**

- Dental conferences, KOL dentists
- Beacon Hospital-style validation sites → reference customers
- Practice subscription tier (metadata: up to 50 patients Professional / unlimited Practice)

**Differentiation vs consumer:**

| | Consumer app | Dentist app |
|---|--------------|-------------|
| Primary UI | Real-time dashboard, device pairing | Participant list, historical trends |
| Data ingress | BLE from device | CloudKit + CSV import |
| Export | Clinical PDF, research CSV | Patient CSV, handshake JSON |
| Subscription | Individual plans | Practice plans |

**Risk:** Dentist app copy is **more clinical** than consumer disclaimers (`DENTIST_APP_STORE_METADATA.md` — "treatment decisions"). Keep aligned with wellness regulatory posture until Path C, or add explicit "for wellness monitoring only" in professional UI.

### 11.3 Path C: Cleared medical device (mid term)

**Buyer:** Health system, sleep clinic, dental sleep medicine, Rx channel

**Product:**

- **Oralable Oral Activity Monitor** with 510(k) / CE label
- Locked SaMD version; UDIs; IFU
- Professional app becomes **clinical decision support** viewer (labeling-dependent)

**Messaging:**

- Cleared intended use only (see §9.2)
- Post-market surveillance, complaint handling, MDR periodic reports

**Channels:**

- Dental sleep medicine practices
- OEM to dental device distributors
- Insurance / employer wellness (only if payer coverage pathway exists — difficult for bruxism)

**Required changes:**

- Separate **cleared** vs **wellness** SKUs or firmware/app build flavors
- `RegulatoryPackageBuilder` outputs become living QMS artifacts
- Sales team cannot use current website wellness copy

### 11.4 GTM decision matrix

| Factor | Path A Consumer | Path B Dentist | Path C Medical |
|--------|-----------------|----------------|----------------|
| Time to revenue | **Fastest** (Jan 2026 target) | Fast (professional app ready) | 18–36+ months |
| Regulatory cost | Low | Low–medium (watch claims) | High (510(k), QMS, clinical) |
| Clinical credibility | Medium | **Higher** (dentist logo) | **Highest** |
| TAM | Large (bruxism-aware consumers) | Medium (dental practices) | Smaller, higher ASP |
| Android required? | Yes for TAM | Less urgent | Labeling-dependent |
| Data strategy | CloudKit opt-in | CloudKit + CSV | Audited, locked versions |

### 11.5 Recommended sequencing

```
2026 Q1–Q2   Path A launch (iOS consumer) + Path B beta (dentist referrals)
2026 Q3–Q4   Android MVP (Path A); Firebase or unified backend for cross-platform share
2027         Clinical study completion; predicate analysis; 510(k) pre-sub meeting
2027–2028    Path C filing; parallel wellness SKU for non-regulated markets
```

**Do not** lead App Store launch with Path C claims. **Do** use Path B dentist channel to generate real-world evidence for Path C.

### 11.6 Competitive GTM positioning

| Competitor GTM | Oralable counter-position |
|----------------|---------------------------|
| JCRing OEM / white-label wellness ring | "Bruxism-specific, not another sleep ring" |
| Oura / WHOOP general readiness | "Jaw occlusion signal rings cannot see" |
| ANR / Cometa EMG | "No electrodes; overnight comfort; validated against EMG" |
| Night guard only | "Continuous monitoring before and after splint" |
| Wellue O2Ring (SpO₂) | "Jaw activity + oxygen burden — rings can't see the masseter" |
| Aktiia / Hilo (BP) | Same **longitudinal pro workflow**; different specialty (cardiology vs dental sleep) |
| SOND Dreambuds | "Coach sleep from the ear; Oralable documents what the jaw did" |

---

## 12. Development trajectory (12–24 months)

### Hardware / firmware

1. Ship **nRF54L15** board when PCB pin map frozen; re-validate cheek RF and IR-DC scaling
2. Partial **on-device inference** (bruxism classifier) using 256 KB RAM headroom
3. **Battery life** characterization for multi-night use
4. **In-app DFU** (optional; Device Manager sufficient for field trials)
5. **Skin temperature** — optional future sensor; rings have it today

### Mobile

1. **CloudKit production** — unblock dentist sharing at scale
2. **Feature flag lift** — HR, SpO₂, Temporalis cards on after validation
3. **Android MVP** — native Kotlin per §10
4. **Unified backend** — if Android share is strategic in 2026 H2
5. **Unified overnight report** UX — TFI + SASHB + events timeline (see §15)

### Algorithm / clinical

1. **EMG cross-validation** at scale (ANR + Cometa where available)
2. **50 Hz pipeline hardening** — SNR checks, baseline wander, sync-tap protocol
3. **Sleep staging** — Attia 50 Hz ambitions from jaw + PPG (secondary claim)
4. **Regulatory dossier population** — `RegulatoryPackageBuilder` + Python reports → QMS

---

## 13. One-page summary

**What Oralable is:** A cheek-worn **MAXM86161 + LIS2DTW12** sensor on **Nordic BLE**, streaming **50 Hz raw PPG and accelerometry** to an **iOS-first** two-app ecosystem, processing **IR-DC hemodynamic occlusion** for **sleep bruxism** — validated against **EMG** (ANR M40) in software, not in the consumer hardware.

**Where it fits:** Orthogonal to **readiness rings** (Oura, JCRing, WHOOP); adjacent to **EMG bruxism tools** (ANR, Cometa); parallel to **longitudinal monitors** (Aktiia/Hilo for BP, Wellue for SpO₂, SOND for sleep coaching) — same **overnight + pro export** product shape, different biomarker and specialty.

**Where it is going:** **nRF54L15** without changing PPG silicon; **Android native** in 2026; **CloudKit/production sharing** for dentist channel; **510(k)** for nocturnal bruxism **monitoring** (not general wellness) when clinical package is ready; unified **overnight report** UX (TFI + SASHB + events) aligned with Hilo/Wellue timeline grammar.

**How to sell it now:** **Path A + B** — wellness consumer launch with dentist referral and professional viewer; accumulate evidence; **Path C** when `RegulatoryPackageBuilder` outputs reflect completed studies and locked algorithms.

**What not to do:** Compete on readiness scores with Oura; claim FDA clearance on website; ship Android with Flutter BLE; market to dentists with diagnostic language before clearance; position as substitute for Wellue SpO₂ or Hilo BP — position as **jaw-specific complement**.

---

## 14. Overnight monitoring peers: SOND, Wellue, Aktiia/Hilo

Consolidated positioning for products discussed alongside rings/straps but serving **different biomarkers** on the **same overnight monitoring shelf**.

### Three lanes on one highway

```
              LONGITUDINAL AMBULATORY MONITORING (overnight-capable)
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
   Cardiovascular        Respiratory           Orofacial / sleep
        │                     │                     │
   Aktiia / Hilo         Wellue O2Ring          Oralable MAM
   (BP, wrist PPG)       (SpO₂, finger)         (jaw IR-DC + SASHB)
        │                     │                     │
        └─────────────────────┼─────────────────────┘
                              │
         Hourly rollups · trends · pro export · treatment feedback
```

### SOND Dreambuds — ear, closed-loop sleep coach

- **Not PPG** — SCG, respiration, snoring, motion, mics; anti-optical positioning vs rings/watches.
- **Intervenes** (adaptive audio) vs Oralable **documents** (events + dentist share).
- **No bruxism** claim; no dental channel.
- **NRF:** not confirmed; audio-centric SoC architecture expected.

### Wellue O2Ring — finger, cleared oximeter

- **Best-in-class consumer SpO₂** at finger (transmissive).
- Overlaps Oralable on **SASHB**, overnight HR, motion, clinician PDF.
- Does **not** measure jaw occlusion or masseter activity.
- Pairing opportunity: **Wellue (O₂) + Oralable (jaw)** in dental sleep / OSA-adjacent patients.

### Aktiia / Hilo — wrist, cleared cuffless BP

- **Regulatory reference** for optical wearable → FDA/CE monitoring claim.
- **Pulse wave morphology** (PWA for BP) parallels Oralable **IR-DC morphology** (occlusion for bruxism) — same class of science, different endpoint.
- Monthly **cuff calibration** vs Oralable **cheek coupling** quality gate.
- Cardiology/GP channel vs **dental** channel.

---

## 15. Longitudinal ambulatory monitoring pattern

Hilo (BP) and Oralable (jaw + overnight physiology) are **different measures in different clinical lanes**, but they share the same **data product shape** — closer to each other than to Oura-style readiness scores.

### Shared product architecture

```
Wearable (passive) → Phone/cloud processing → Time-series + burden scores → User app → Clinician view
```

| Layer | **Hilo / Aktiia** | **Oralable** |
|-------|-------------------|--------------|
| Raw signal | Green PPG @ wrist | R/G/IR PPG + ACC @ cheek |
| Derived metrics | BP mmHg, HR, time-in-target | TFI, SASHB, Temporalis states, rescue events |
| Sampling | Sparse when still (~2 BP/hr) | Dense when worn (50 Hz → hourly rollups) |
| User question | "What's my BP pattern over days?" | "What did my jaw and oxygen do last night?" |
| Pro question | "Is treatment working?" | "Is splint/therapy reducing bruxism and burden?" |

### Multi-condition hub (not one label per device)

**Hilo — BP as hub:**

- Hypertension (primary), cardiovascular risk, nocturnal dipping, medication response, stress/lifestyle context.

**Oralable — jaw + overnight physiology as hub:**

- Sleep bruxism (primary), TMJ/orofacial pain, splint efficacy, **SASHB** / sleep-breathing overlap, rescue clench vs desaturation (protocol-validated).

Both answer: *"What happened while I wasn't in the clinic, and how does that connect to conditions my clinician cares about?"*

### Shared visualization grammar

Clinicians and users want **temporal structure**, not raw waveforms first.

| Chart pattern | Hilo (typical) | Oralable (in codebase) |
|---------------|----------------|------------------------|
| Night timeline | BP by hour, time-in-target | `ProfessionalHourlyRollupExport` per hour |
| Burden / area-under-curve | Hypertensive exposure | **SASHB** (%·s hypoxic burden), **TFI** |
| Event markers | Out-of-range BP readings | Quiet / phasic / tonic / **rescue** + `rescueEventCount` |
| Multi-night trends | Week/month averages | `SessionHistoryStore` across sessions |
| Correlation view | BP vs time of day | Clinical PDF SpO₂–clench "smoking gun" |
| Visit export | PDF / provider dashboard | Handshake JSON + Clinical Temporalis PDF |

Oralable handshake export (`OralableCore/CloudKit/ProfessionalHandshakeExport.swift`) is structurally analogous to Hilo's **hourly clinical bins** — compressed physiology for a short appointment.

### Shared user ↔ professional loop

| Step | Hilo / Aktiia | Oralable |
|------|---------------|----------|
| Home wear | Bracelet, passive when still | Cheek clip, auto-record when worn |
| Setup anchor | **Cuff calibration** (monthly) | Placement, worn detect, firmware gate |
| Consumer app | Trends, alerts | Dashboard, TFI/SASHB, share |
| Share with clinician | Healthcare dashboard | 6-digit code, CloudKit, handshake JSON |
| Professional app | Hilo clinical tools | **Oralable for Dentists** |
| Visit prep | "Last 30 days of BP" | Bruxism PDF / handshake export |
| Treatment loop | Med change → BP trend | Splint/therapy → TFI trend |

Both sell **continuity of care outside the chair** — not a single snapshot at a visit.

### Strategic implications for Oralable

1. **Positioning** — Ambulatory, passive, longitudinal, clinician-shareable (same category as Hilo, not "another sleep ring").
2. **UI borrow** — Night timelines, time-in-target, week-over-week deltas map onto TFI/SASHB/rescue charts.
3. **Professional product** — Aktiia healthcare dashboard is a template for scaling dentist cohort views and treatment-response alerts.
4. **Regulatory narrative** — Aktiia: optical PPG + validation → cleared monitoring. Oralable: IR-DC + EMG concordance → **nocturnal jaw activity monitoring** (different predicate, same evidence *class*).
5. **Combined patient panel** — In dental sleep medicine, BP + bruxism + oxygen often coexist; Hilo + Oralable + Wellue are **complementary panels**, not substitutes.

### Where the parallel ends

| Hilo | Oralable |
|------|----------|
| Cleared BP in mmHg | Wellness today; bruxism monitoring path |
| Cuff calibration required | IR-DC coupling 10M–70M; no cuff |
| Cloud-heavy OBPM | Phone + open 50 Hz pipeline |
| Cardiology / GP | **Dental / sleep dentist** |
| Wrist green-only PPG | Cheek R/G/IR + ACC |

### Product opportunity: unified overnight report

Natural next UX spec — **one page per night** for consumer and dentist:

- TFI timeline by hour
- SASHB band / hypoxic segments
- Rescue events overlaid on SpO₂ context
- HR strip (secondary)

Mirrors Hilo's **nocturnal BP profile** and Wellue's **SpO₂ drop chart** — same report card grammar, Oralable-specific biomarkers.

---


---

## Appendix A: Nordic wearables comparison

## By Nordic chip family

### nRF52832 (Cortex-M4, 512 KB Flash, 64 KB RAM)

Best fit: **smallest rings and sensor-heavy bands** where BOM and PCB area matter most. Same chip family as **Oralable pcb00003**.

| Product | Form | Primary role of nRF52832 | Sensors / notes | Battery (claimed) | SDK / stack |
|---------|------|--------------------------|-----------------|-------------------|-------------|
| **Oralable** (pcb00003, shipping) | Cheek / jaw clip | BLE + app MCU, PPG/ACC/temp | MAXM86161 PPG (R/G/IR), LIS2DTW12 ACC, MCUboot + mcumgr OTA | TBD (design target: multi-day) | nRF Connect SDK (Zephyr) |
| **Oralable** (next gen, planned) | Cheek / jaw clip | Kaga **ES4L15BA1** / **nRF54L15** | **MAXM86161EFD+** (same PPG), see [roadmap](./HARDWARE_ROADMAP_nRF54L15.md) | TBD | nRF Connect SDK, BLE 6.0 |
| **ArcX** | Ring (or strap mount) | BLE + remote-control logic | Thumb joystick; no health PPG stack | ~5 days use / 20 days standby | nRF5 SDK era |
| **Polar Vantage V / M** | Watch | BLE link to phone | 9-LED optical HR, GPS, baro (V model) | Multi-day | nRF5 SDK + S132 SoftDevice |
| **GECA Watch 1.0** | Watch | BLE + hydration algo | Optical hydration sensing | Multi-day | nRF Connect SDK |

**Typical limits:** Tighter Flash/RAM than nRF52840; fine for BLE + modest on-device algorithms; heavy ML or many concurrent services may need external MCU or a larger Nordic part.

---

### nRF52840 (Cortex-M4F, 1 MB Flash, 256 KB RAM, CryptoCell)

Best fit: **health rings and bands** that run **sensor fusion + security** on one chip. Most common Nordic chip in commercial health rings today.

| Product | Form | Primary role of nRF52840 | Sensors / notes | Battery (claimed) | SDK / stack |
|---------|------|--------------------------|-----------------|-------------------|-------------|
| **WHOOP 4.0** | Wrist / bicep strap | Documented by Nordic as BLE + algorithm supervisor | HR, HRV, SpO₂, skin temp, sleep, strain; Sila anode battery | Up to **5 days** continuous | Nordic cites nRF52840; teardowns also report **Maxim MAX32652** + **MAX86171** AFE — likely multi-chip (BLE/compute split) |
| **JCRing** (J-Style / Joint Chinese) | Ring | BLE + health algorithms | PPG, SpO₂, temp, HRV, sleep, activity; OEM/white-label platform | Up to **~30 days** light use (vendor); **7+ days** typical (X3) | nRF Connect SDK / nRF5 lineage |
| **Ultrahuman Ring Air** | Ring | BLE (dedicated wireless MCU) | PPG LEDs + photodiode; **STM32G0** handles sensors/PM | Multi-day | nRF5 SDK → migrated to **NCS (Zephyr)** per Ultrahuman engineering blog |
| **Withings ScanWatch Nova** | Hybrid watch | BLE + sensor supervision | Multi-wavelength PPG, ECG, temp, SpO₂, sleep | **~30 days** | nRF Connect SDK |
| **Festina Connected D** | Hybrid watch | BLE + activity UI bridge | Physical hands + OLED window | Multi-day | nRF Connect SDK |

**WHOOP note:** Include WHOOP here because Nordic officially announced **WHOOP 4.0 on nRF52840**, but it is a **strap**, not a ring. **WHOOP 5.0** moved to **Ambiq Micro Cortex-M4 + integrated BLE** (no Nordic in teardown BOM) with **ADI MAX86171** PPG/ECG AFE and **14+ day** battery claims.

---

### nRF5340 (Dual Cortex-M33: app + network cores)

Best fit: **wrist wearables** needing more compute, **LE Audio**, or heavier real-time algorithms — usually **not** inside the tiniest rings.

| Product | Form | Primary role of nRF5340 | Sensors / notes | Battery (claimed) | SDK / stack |
|---------|------|---------------------------|-----------------|-------------------|-------------|
| **Polar Loop** | Wellness band | BLE + biosignal processing | Always-on wellness metrics | Multi-day | nRF Connect SDK (Zephyr) |
| **GECA Watch 2.0** | Watch | BLE + hydration ML pipeline | Optical hydration; upgrade path from nRF52832 v1 | Multi-day | nRF Connect SDK |

---

### nRF54L15 (Next-gen single-core, higher perf/W)

Best fit: **new flagship rings** with dense sensor packs and sports modes on-device.

| Product | Form | Primary role of nRF54L15 | Sensors / notes | Battery (claimed) | SDK / stack |
|---------|------|--------------------------|-----------------|-------------------|-------------|
| **IDO IDR01** | Ring | BLE + on-device sports/health algos | PPG, accelerometer, skin temp; 18 sport modes (vendor) | Multi-day | nRF Connect SDK |
| **Oralable** (planned) | Cheek / jaw clip | Kaga ES4L15BA1 module | MAXM86161EFD+ PPG, LIS2DTW12 ACC, bruxism/sleep stack | TBD | nRF Connect SDK, BLE 6.0 |

---

## Side-by-side: rings + WHOOP (Oralable-relevant)

| | **Oralable** | **ArcX** | **JCRing X3** | **Ultrahuman Air** | **Oura Gen 4** | **WHOOP 4.0** | **WHOOP 5.0** |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Form** | Cheek clip | Finger ring | Finger ring | Finger ring | Finger ring | Wrist strap | Wrist strap |
| **Wireless MCU** | nRF**52832** | nRF**52832** | nRF**52840** | nRF**52840** + STM32G0 | Infineon **PSoC 6** | nRF**52840**† | **Ambiq** + BLE |
| **PPG AFE** | **MAXM86161** | — | Multi-λ module (IC **undisclosed**; see [PPG doc](#appendix-b-ppg-sensor-comparison)) | On-PCB LEDs/PD | ADI **MAX86178** | Maxim **MAX86171** | ADI **MAX86171** |
| **Primary use** | Sleep bruxism / jaw PPG | Media remote | General health OEM | Metabolic/fitness | Sleep & readiness | Strain/recovery | + ECG (MG tier) |
| **Flash / RAM** | 512K / 64K | 512K / 64K | 1M / 256K | 1M / 256K (+ G0) | PSoC 6 class | 1M / 256K† | Ambiq + NAND |
| **OTA** | MCUboot + mcumgr SMP | Vendor | Vendor / OEM | NCS OTA (migrated) | Vendor | Vendor | Vendor |
| **Nordic stack** | NCS (Zephyr) | nRF5 | NCS / nRF5 | NCS (Zephyr) | ModusToolbox | NCS cited | Non-Nordic |

† WHOOP 4.0: Nordic [press release](https://www.nordicsemi.com/Nordic-news/2022/07/The-WHOOP-4-uses-Nordics-nRF52840-SoC); [TechInsights teardown](https://www.techinsights.com/products/ddt-2111-806) also identifies Maxim MAX32652 on the sensor path.

---

## Chip selection pattern (what brands optimize for)

```
                    ┌─────────────────────────────────────────┐
  Smallest / cheapest│ nRF52832  │ ArcX, Oralable, early Polar │
                    ├───────────┼─────────────────────────────┤
  Health ring sweet │ nRF52840  │ JCRing, Ultrahuman, Withings│
  spot (2022–2025)  │           │ WHOOP 4.0 (strap)           │
                    ├───────────┼─────────────────────────────┤
  More compute /    │ nRF5340   │ Polar Loop, GECA 2.0        │
  dual-core         │           │                             │
                    ├───────────┼─────────────────────────────┤
  Next-gen rings    │ nRF54L15  │ IDO IDR01                   │
                    └───────────┴─────────────────────────────┘

  Non-Nordic anchors: Oura (PSoC 6), WHOOP 5 (Ambiq), Samsung Galaxy Ring (Samsung SIP)
```

---

## Nordic wearables that measure EMG (electromyography)

Few consumer rings measure **true sEMG** (skin surface bioelectric potentials). EMG+Nordic shows up more in **armbands**, **assistive switches**, and **research nodes**. Typical architecture: **nRF52 = BLE + MCU**, plus a **dedicated EMG analog front-end** (custom analog, or TI **ADS129x**), unlike PPG rings that often use an integrated optical module.

| Product | Form | Nordic MCU | EMG type | Rate / channels (public) | Primary use |
|---------|------|------------|---------|--------------------------|-------------|
| **Control Bionics NeuroNode** | Skin patch / wearable switch | **nRF52832** ([Nordic PR](https://www.nordicsemi.com/Nordic-news/2018/07/nRF52832-SoC-provides-wireless-connectivity-for-Control-Bionics)) | Surface EMG muscle “switch” | Single site; assistive latency-focused | ALS/MND communication & device control |
| **Neeno 2 Armband** | Forearm wearable | **nRF52840** ([hardware docs](https://docs.myneeno.com/hardware/)) | **Surface EMG** + IMU | **40–400 Hz** ODR; ~28 ms latency | Rehab, gesture, developer SDK |
| **Myo Armband** (Thalmic, legacy) | Forearm band | **nRF51822** (BLE) + Freescale Kinetis M4 ([Adafruit teardown](https://learn.adafruit.com/myo-armband-teardown)) | **8-channel** surface EMG | EMG + IMU gesture classification | Consumer gesture control |
| **uMyo** | Open-source patch | **nRF52832** ([GitHub](https://github.com/ultimaterobotics/uMyo)) | Single-channel sEMG + IMU + magnetometer | BLE stream to phone/PC | Maker / robotics / fitness |
| **IOMICO EMG Tracker** | Skin-contact pod | **nRF52840** (Zephyr/NCS, [case study](https://www.iomico.com/case-studies/emg-tracker-for-wellness-and-healthcare)) | Muscle strength / activation | Live chart to Flutter app; multi-device BLE sync | Gym form / training feedback |
| **Limbitless bionic hand** | Pediatric prosthetic | **Insight ISP1507** (**nRF52832** module) ([Nordic blog](https://blog.nordicsemi.com/getconnected/smart-limbs-make-heroes-by-design)) | EMG **stickers** on arm → hand actuation | Config via BLE app | Low-cost myoelectric prosthetic |
| **Research module** (MDPI / academic) | Wireless bio sensor board | **nRF52840** + **TI ADS1293** AFE ([paper](https://www.mdpi.com/2079-9292/9/6/934)) | Up to **3× EMG** (+ ECG, 6-DoF IMU) | Up to **3.2 kHz**/ch, 24-bit | Activity monitoring, nursing tech |
| **ANR M40 Muscle Sense** | Wearable EMG pod | **Not published** (BLE to iOS/Android) | 2-lead surface EMG | Muscle activation level (proprietary) | Oralable app **comparison** device — see note below |

### EMG vs Oralable (PPG / IR-DC)

| | **EMG wearables above** | **Oralable** |
|---|-------------------------|--------------|
| Signal | Electrical potential at skin (µV) | **Hemodynamic** PPG + **IR DC** muscle occlusion |
| Site | Forearm, assistive muscle, prosthetic | **Masseter / cheek** |
| Nordic role | BLE + often heavy DSP on-chip | BLE + 50 Hz PPG pipeline |
| Fusion | Often IMU for motion artifact | **Accelerometer** for jaw vibration / actigraphy |

Oralable targets **sleep bruxism via blood-flow occlusion**, not electrode-based EMG. The iOS app pairs **ANR M40** for research-side **EMG comparison**, but ANR does not document a Nordic SoC.

### EMG chip pattern (when documented)

```
  Skin electrodes → EMG AFE (ADS1293, custom analog, or SoC ADC) → nRF52/54 → BLE → app
```

Reference designs often use **nRF52840 + ADS1293** because EMG needs **high input impedance**, **low noise**, and **kHz sampling** — requirements the MAXM86161 optical path does not address.

---

## Oralable positioning

| Dimension | Oralable choice | Implication |
|-----------|-----------------|-------------|
| MCU | **nRF52832** today → **nRF54L15** (roadmap) | Next gen aligns with IDO IDR01 tier; see [HARDWARE_ROADMAP_nRF54L15.md](./HARDWARE_ROADMAP_nRF54L15.md) |
| Location | Masseter / cheek | Different optical path than finger PPG rings; closer to muscle occlusion + jaw vibration |
| Sensors | MAXM86161 (R/G/IR) + ACC | **Named PPG part** vs JCRing/IDO undisclosed modules — see [Appendix B](#appendix-b-ppg-sensor-comparison) |
| Muscle signal | IR-DC + ACC (not sEMG) | Compare against **Neeno / NeuroNode / ANR M40** EMG path in validation studies |
| OTA | MCUboot + mcumgr (Nordic Device Manager compatible) | Same OTA ecosystem as NCS health devices |
| Upgrade path | nRF52840 or nRF54L15 if on-device ML outgrows 512K Flash | JCRing/Ultrahuman/WHOOP 4.0 class |

**Overnight monitoring peers** (different biomarker, similar longitudinal product shape): Wellue O2Ring (SpO₂), Aktiia/Hilo (BP), SOND Dreambuds (sleep coaching) — see §14–15.

---

## References

- [WHOOP 4.0 + nRF52840 (Nordic)](https://www.nordicsemi.com/Nordic-news/2022/07/The-WHOOP-4-uses-Nordics-nRF52840-SoC)
- [ArcX + nRF52832 (Nordic)](https://www.nordicsemi.com/Nordic-news/2021/02/The-ArcX-smart-ring-uses-nRF52832-to-enable-remote-control-of-music-and-calls-for-people-on-the-move)
- [JCRing + nRF52840 (Nordic)](https://www.nordicsemi.com/Nordic-news/2024/05/Joint-Chinese-LTD-chooses-Nordic-Semiconductor-for-next-generation-smart-ring)
- [IDO IDR01 + nRF54L15 (Nordic)](https://www.nordicsemi.com/Nordic-news/2025/10/IDOs-IDR01-smart-ring-integrates-Nordics-nRF54L15-SoC)
- [Ultrahuman NCS migration (Memfault Interrupt)](https://interrupt.memfault.com/blog/upgrading-from-nrf5-sdk-to-ncs)
- [Ultrahuman Ring Air teardown (Making Studio)](https://makingstudio.blog/2024/09/10/ultrahuman-ring-air-teardown/)
- [Oura Ring 4 teardown (TechInsights)](https://www.techinsights.com/blog/oura-ring-gen-4-teardown)
- [WHOOP 5.0 teardown summary (EEPW)](https://www.eepw.com.cn/article/202603/479523.htm)
- [Oralable OTA workflow](./OTA_DEVICE_MANAGER.md)
- [PPG sensor comparison (JCRing, IDO, Polar, Withings)](#appendix-b-ppg-sensor-comparison)
- [Control Bionics NeuroNode + nRF52832 (Nordic)](https://www.nordicsemi.com/Nordic-news/2018/07/nRF52832-SoC-provides-wireless-connectivity-for-Control-Bionics)
- [Neeno 2 hardware (nRF52840 + sEMG)](https://docs.myneeno.com/hardware/)
- [uMyo open-source EMG (nRF52832)](https://github.com/ultimaterobotics/uMyo)
- [ANR M40 Muscle Sense](https://www.anrcorp.com/product/m40-muscle-sense/)

---

## Appendix B: PPG sensor comparison

## At-a-glance

| Product | Form | PPG approach | Named PPG IC / module? | LEDs / receivers (public) | SpO₂ | Notes |
|---------|------|--------------|------------------------|---------------------------|:----:|-------|
| **Oralable** | Cheek clip | **ADI MAXM86161** integrated optical module | **Yes** — MAXM86161EFD+ | 3 LED (R/G/IR) + 1 PD in package | Via IR/G | Same silicon on pcb00003 and nRF54L15 roadmap |
| **JCRing** (J-Style X3/X6) | Finger ring | Multi-wavelength / multi-channel PPG module | **No** public IC; B2B listings cite **Goodix** on some SKUs | Dual/multi-wavelength + **IR for night**; 3-axis ACC | Yes | nRF52840; OEM white-label platform |
| **IDO IDR01** | Finger ring | Built-in PPG (vendor) | **No** | Undisclosed | Yes (claimed) | nRF54L15; ODM — likely module similar to JCRing class |
| **Ultrahuman Ring Air** | Finger ring | Discrete on-PCB PPG + **nRF52840** BLE | **No**; teardown: LEDs + PD on flex | Green + red LEDs, photodiode | Yes | **STM32G0** runs sensor path |
| **Polar Vantage V/M/V2** | Watch | **Precision Prime™** custom optical bump | **No** | **~9–10 LEDs**, multiple PDs, accel, skin-contact electrodes | Limited / sport HR focus | nRF52832 BLE |
| **Polar Loop** | Band | **Precision Prime™ GEN 3.5** (cost-optimized) | **No** | Smaller optical stack than Elixir watches | — | nRF5340 |
| **Polar Vantage V3+** | Watch | **Elixir™** — Gen 4 OHR + SpO₂ GEN 1 + ECG | **No** | Redesigned bump; separate SpO₂ + ECG paths | Yes | Not MAXM86161-style integrated module |
| **Withings ScanWatch** | Hybrid watch | Reflective PPG (clinical paper) | **No** | **3 LEDs** (R, IR, G) + **2 photodiodes** | Yes | [JMIR validation](https://www.jmir.org/2021/4/e27503/) |
| **Withings ScanWatch 2 / Nova** | Hybrid watch | **PPG Sensor Gen 3**, 16-channel multi-wavelength | **No** | 4 wavelengths, 16 channels (vendor) | Yes | nRF52840; HealthSense / PowerSense |
| **Oura Ring 4** | Finger ring | Discrete multi-path optical | **Yes** (teardown) — **ADI MAX86178** AFE | 2 multi-color LEDs + 3 photodiodes | Yes | Infineon PSoC 6 MCU |
| **WHOOP 4.0** | Strap | Discrete optical stack | Teardown: **Maxim MAX86171** + MAX32652 | 5 LEDs, 4 PDs (typical config) | Yes | Nordic cites nRF52840 |
| **WHOOP 5.0 / MG** | Strap | Discrete PPG/ECG | Teardown: **ADI MAX86171** | Same LED array class as 5.0 | Yes + ECG (MG) | Ambiq MCU, not Nordic |
| **Wellue O2Ring** | Finger ring | **Transmissive** R+IR through finger | **No** (Viatom integrated stack) | 660 nm + 940 nm; 1 PD opposite LEDs | Yes (primary) | **FDA oximeter** 2025; not reflective cheek PPG |
| **Aktiia / Hilo** | Wrist band | Reflective **green** 526 nm PPG | **No** | 1λ + PD | No (BP focus) | CE IIa + FDA OTC cuffless BP; see §Aktiia |

---

## JCRing (J-Style / Joint Chinese) — detail

JCRing is the closest **commercial peer** to Oralable among Nordic health rings: same broad market (HR, HRV, SpO₂, sleep, temp), often used as an **OEM/white-label** reference design.

### Wireless MCU (documented)

| Item | Value |
|------|--------|
| SoC | **nRF52840** ([Nordic press release](https://www.nordicsemi.com/Nordic-news/2024/05/Joint-Chinese-LTD-chooses-Nordic-Semiconductor-for-next-generation-smart-ring), May 2024) |
| Role | BLE + on-device health algorithms |
| Form | ~**8.0 mm × 2.7–2.9 mm** ring, titanium, 5ATM |
| Battery | **15.5–23.5 mAh** LiPo (model-dependent); vendor claims **7–10+ days** |

### PPG hardware (public — no single part number)

J-Style does **not** publish a datasheet-quality PPG IC (e.g. MAXM86161) for retail JCRing models. Public materials describe:

| Claim (J-Style / JCRing) | Implication |
|--------------------------|-------------|
| **Multi-channel / multi-wavelength PPG** | More than one wavelength path — not a single integrated OLGA like MAXM86161 |
| **Dual-wavelength optical engine** | At least two LED wavelengths (often green + red or red + IR) |
| **Infrared LED for nighttime / dark skin** | Separate IR emphasis for sleep SpO₂ and SNR |
| **Medical-grade LED wavelengths** | Marketing term — actual peak λ not always specified |
| **3D accelerometer** | Motion rejection for PPG (same class as Polar fusion, simpler than Precision Prime electrodes) |
| **Skin temperature sensor** | Separate from PPG die |
| **SDK/API raw PPG stream** | B2B customers can access HR, HRV, SpO₂, motion — suggests custom firmware on nRF52840 |
| **"Goodix Sensor"** on some [Made-in-China B2B SKUs](https://m.madeinchina.com/mall/show-Jcring-J-Style-Smart-Rings-Heart-Rate-Monitoring-SpO2-Monitor_1180771.html) | **Unconfirmed** for JCRing X3 retail — may be SKU-specific or listing shorthand |

**Likely architecture (inferred, not confirmed):** discrete **PPG AFE or module** (Goodix GH3026/GH3220 class, Tianyi HX36xx, or similar) + discrete LEDs/PDs on ring flex — **not** a single MAXM86161 package.

### JCRing vs Oralable PPG

| Dimension | **JCRing** | **Oralable** |
|-----------|------------|--------------|
| PPG part | Undisclosed module / multi-channel | **MAXM86161EFD+** (named, integrated) |
| Wavelengths | Multi-wavelength + IR night (vendor) | **R + G + IR** in one module |
| Integration | Module or discrete stack on ring PCB | Single OLGA (2.9 × 4.3 × 1.4 mm) |
| Site | Finger (vascular bed) | **Cheek / masseter** (occlusion + jaw vibration) |
| MCU | nRF52840 (shipping) | nRF52832 → **nRF54L15** (roadmap) |
| Algorithm focus | General wellness, SpO₂, sleep, stress | **Sleep bruxism**, IR-DC coupling, 50 Hz pipeline |
| OEM | Full ODM — many white-label rings | Custom product |

Oralable’s MAXM86161 path is **more integrated and BOM-transparent**; JCRing’s is **more flexible for OEM** (swap optical module per customer) but **less documented** at the silicon level.

---

## IDO IDR01

| Item | Detail |
|------|--------|
| PPG | Listed as one of three sensors (PPG, accel, skin temp) — [Nordic IDR01 news](https://www.nordicsemi.com/Nordic-news/2025/10/IDOs-IDR01-smart-ring-integrates-Nordics-nRF54L15-SoC) |
| Part number | **Not disclosed** |
| Peer guess | Same ODM ecosystem as JCRing (Shenzhen wearable factories) — architecture likely **similar tier** to JCRing, not Oura MAX86178 class |

---

## Polar

Polar never sells a catalog PPG chip — they sell **Precision Prime** and **Elixir** optical **systems**.

### Precision Prime (Vantage V/M/V2, Loop)

- **~9–10 LEDs** (red, green, orange/yellow combinations vary by generation)
- **Multiple photodiodes**
- **3D accelerometer** for motion artifact rejection
- **Electrode posts** for skin-contact quality (unique vs rings)
- Custom raised **optical bump** pressed into wrist

[DC Rainmaker Vantage V review](https://www.dcrainmaker.com/2018/12/polar-vantage-multisport-review.html) · [Polar optical HR white paper](https://www.polar.com/img/static/whitepapers/pdf/polar-optical-heart-rate-white-paper.pdf)

### Elixir (Vantage V3+)

- **Gen 4 OHR** + **SpO₂ GEN 1** + **ECG GEN 1** + skin temperature
- Sensor fusion brand; still **no public AFE part number**

---

## Withings

### ScanWatch (original)

Validated in peer-reviewed study: **3 LEDs** (red, infrared, green) + **2 photodiodes** (broadband + IR-cut), reflective wrist PPG.

### ScanWatch 2 / Nova

- **PPG Sensor Gen 3**
- **16-channel multi-wavelength** (Nova product specs)
- **PowerSense™ Gen 3** — likely custom flex + discrete AFE (Oura 4 uses **MAX86178** in same product class, but Withings BOM not public)

---

## Architecture tiers (where JCRing sits)

```
  Most integrated                    Most custom / multi-LED
  ─────────────────────────────────────────────────────────────►

  MAXM86161          JCRing / IDO        Ultrahuman        Withings Gen3      Polar Elixir      Oura 4 / WHOOP 5
  (Oralable)         (multi-λ module)    (PCB LEDs+PD)     (16-ch module)     (9-10 LEDs)       (MAX86178/71 AFE)
       │                  │                    │                  │                 │                  │
   1 package         ODM ring stack      nRF + STM32G0      watch module      wrist bump         finger/strap
```

**JCRing** sits in the **middle-left**: more integrated than Polar/Withings watches, less transparent than Oralable’s single MAXM86161 part, similar **ODM ring** bucket to **IDO IDR01**.

---

## Wellue / Viatom O2Ring (transmissive finger oximeter)

Not a wellness ring — a **clinical-style pulse oximeter** in ring form. **FDA-cleared** Smart Ring Pulse Oximeter (2025).

| Item | Detail |
|------|--------|
| PPG | **Transmissive** Red 660 nm + IR 940 nm through finger (not reflective cheek PPG) |
| Output | SpO₂ %, pulse rate, motion — ~1 Hz updates; app charts ~4 s |
| vs Oralable | Wellue **owns finger SpO₂**; Oralable **owns cheek IR-DC + jaw ACC + SASHB** |
| BLE | Proprietary (`14839ac4…` legacy or OxyII on O2Ring-S) — not TGM open GATT |

**Complementary:** Wellue (oxygen) + Oralable (jaw activity) in dental sleep / OSA-adjacent workflows. See [ORALABLE_MARKET_LANDSCAPE.md](./ORALABLE_MARKET_LANDSCAPE.md) §7.6.

---

## Aktiia / Hilo (wrist green PPG → blood pressure)

**Cuffless BP** via green PPG (526 nm) + pulse wave analysis + **monthly cuff calibration**. **CE Class IIa** + **FDA 510(k) OTC** (2025).

| Item | Detail |
|------|--------|
| PPG | Single-green reflective at wrist — **not** multi-wavelength SpO₂ stack |
| vs Oralable | Same **longitudinal ambulatory monitoring** product shape (user + clinician trends); different biomarker (BP vs TFI/SASHB/bruxism) |
| Regulatory | Reference path for optical wearable clearance — see landscape §9–15 |

---

## Oralable takeaway

| Choice | Rationale |
|--------|-----------|
| Stay on **MAXM86161** | Named part, existing Zephyr driver, cheek R/G/IR + IR-DC bruxism path proven on pcb00003 |
| vs **JCRing-style** multi-module PPG | JCRing optimizes for finger SpO₂/sleep SKU variety; Oralable optimizes for **masseter optical occlusion** with a known integrated sensor |
| vs **Polar/Withings** discrete stacks | Those need wrist-scale LED count and ECG/SpO₂ rails; overkill for cheek clip BOM and layout |
| MCU roadmap **nRF54L15** | Puts Oralable in **IDO IDR01 compute tier** while keeping a **more documented** PPG than JCRing publicly specifies |

---

## References

- [JCRing + nRF52840 (Nordic)](https://www.nordicsemi.com/Nordic-news/2024/05/Joint-Chinese-LTD-chooses-Nordic-Semiconductor-for-next-generation-smart-ring)
- [J-Style Smart Ring FAQ](https://www.jointcorp.com/j-style-smart-ring-faq-ultimate-2025-guide/)
- [JCRing X6B product page](https://jcring.tech/pages/x6b-smart-health-ring-details)
- [IDO IDR01 + nRF54L15 (Nordic)](https://www.nordicsemi.com/Nordic-news/2025/10/IDOs-IDR01-smart-ring-integrates-Nordics-nRF54L15-SoC)
- [Withings ScanWatch SpO₂ validation (JMIR)](https://www.jmir.org/2021/4/e27503/)
- [Oura Ring 4 teardown (TechInsights)](https://www.techinsights.com/blog/oura-ring-gen-4-teardown)
- [MAXM86161 datasheet / Oralable roadmap](./HARDWARE_ROADMAP_nRF54L15.md)
- [Nordic wearables MCU comparison](#appendix-a-nordic-wearables-comparison)
- [Market landscape (Wellue, Aktiia, SOND)](./ORALABLE_MARKET_LANDSCAPE.md)
- [Wellue O2Ring](https://getwellue.com/products/o2ring-wearable-pulse-oximeter)
- [Aktiia / Hilo FDA OTC clearance](https://www.prnewswire.com/news-releases/aktiias-hilo-band-becomes-first-cuffless-blood-pressure-monitor-cleared-by-fda-for-over-the-counter-use-302501123.html)

## References

- [HARDWARE_ROADMAP_nRF54L15.md](./HARDWARE_ROADMAP_nRF54L15.md)
- [OTA_DEVICE_MANAGER.md](./OTA_DEVICE_MANAGER.md)
- [DEVELOPMENT.md](./DEVELOPMENT.md)
- `oralable_swift/OralableApp/LAUNCH_READINESS_CHECKLIST.md`
- `oralable_swift/OralableApp/CLOUDKIT_PRODUCTION_SETUP.md`
- `cursor_oralable/docs/CLINICAL_VALIDATION.md`
- [SOND](https://sond.com/) · [TechCrunch SOND launch](https://techcrunch.com/2026/05/27/sond-a-sleep-tech-startup-from-boses-former-head-of-sleep-exits-stealth-with-7m/)
- [Wellue O2Ring](https://getwellue.com/products/o2ring-wearable-pulse-oximeter) · [AASM FDA approval note](https://aasm.org/apnimed-announces-positive-results-in-clinical-trial-of-sleep-apnea-medication-2-2/)
- [Aktiia / Hilo](https://hilo.com/) · [FDA OTC clearance PR](https://www.prnewswire.com/news-releases/aktiias-hilo-band-becomes-first-cuffless-blood-pressure-monitor-cleared-by-fda-for-over-the-counter-use-302501123.html)
- `OralableCore/Sources/OralableCore/CloudKit/ProfessionalHandshakeExport.swift`

---

## Documentation index (all repos)

| Hub | Path |
|-----|------|
| **Firmware + market** | `oralable_nrf/docs/README.md` |
| **Algorithms + clinical** | `cursor_oralable/docs/README.md` |
| **Launch / CloudKit** | `oralable_swift/OralableApp/LAUNCH_READINESS_CHECKLIST.md` |

---

*Document version: 1.2 — June 2026. Appendices A (Nordic) and B (PPG) consolidated; see DEVELOPMENT.md and cursor_oralable CLINICAL_VALIDATION.md.*
