# Gen2 Program Gate — nRF54L15 / PCB00003 REV11

**Program status:** Evidence-gated and non-release. Gen2 is not a release process, a product release path, a pilot-device claim, an OTA claim, or an approved replacement for Gen1.

The machine-readable source of truth is [`gen2.program.json`](../gen2.program.json). The checked-in build helpers can create local non-release candidate outputs, but a successful build does not close a hardware gate and does not authorize programming, OTA, artifact promotion, support, marketing, or a release claim.

## Two deliberately separate build tracks

`gen2_bringup` builds `bringup_ble/`, a bench advertiser that powers the board, advertises, accepts a connection, and exposes a marker characteristic. It is intentionally not the Oralable GATT or sensor application.

`gen2_patient_candidate` builds `app/` against a separate NCS v2.9.3 workspace. It is a non-release port used only to gather evidence after applicable prerequisite gates have been planned. It does not create a release artifact or replace any existing `artifacts/` file.

Both tracks require an explicit NCS workspace and Nordic toolchain path. They never invoke a programming, flash, OTA, or artifact-promotion command.

```sh
# Create the pinned, separate SDK workspace once. This needs network access.
./scripts/bootstrap_gen2_ncs.sh --ncs-root /path/to/ncs-v2.9.3

# Build non-release local candidates only.
./scripts/build_gen2_bringup.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/ncs-toolchain --pristine
./scripts/build_gen2_app.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/ncs-toolchain --pristine

# Validate source controls. This passes while the release gate remains closed.
python3 scripts/verify_gen2_program.py
```

The toolchain and SDK must match the revisions in [`firmware.toolchains.json`](../firmware.toolchains.json). A local NCS installation, a cached build, an untracked hex, or an observed advertiser does not override the manifest.

## Required evidence gates

Every gate starts pending in `gen2.program.json`. Change a gate only after the listed evidence has a durable location, immutable revision/hash, date, hardware identifier, responsible tester, observed result, and independent reviewer. Never infer a passed state from code review or from a build alone.

### G2-P2 — BLE identity and GATT parity

Show the firmware version, device identity, complete TGM GATT inventory, connection lifecycle, notification subscription, disconnect, and reconnect against the versioned TGM contract. The `bringup_ble` marker service does not satisfy this gate.

### G2-P3 — stream parity

Collect time-stamped PPG and accelerometer captures, demonstrate the expected packet formats and rates, parse them through the shared BLE contract, and account for drops and errors in that capture. “Same GATT” is a hypothesis until this evidence exists.

The accelerometer rate for this candidate is 50 Hz. Firmware `CTRL1` is `0x43`. `OralableCore` and `src/parser/log_parser.py` stamp samples at 20 ms, and the iOS `AlgorithmSpec` declares 50 Hz. A clean candidate is still required before a capture can be parity evidence. Use the sibling [`GEN2_STREAM_EVIDENCE.md`](../../cursor_oralable/docs/GEN2_STREAM_EVIDENCE.md) procedure and its offline `verify_gen2_stream_capture.py` tool to check capture metadata, raw packet shapes, counters, delivery cadence, and frame gaps. A passing capture-integrity check is not a G2-P3 pass and cannot update this manifest.

### G2-P4 — battery, charging, state, and LED behavior

Characterize the Gen2 LP270829 cell and charging behavior. Capture BATEN, BATVOL, CHRSTS, dock/placement, and LED transitions. Gen1 CG-320B voltage thresholds and status behavior must not be copied to Gen2 without measured justification.

### G2-P6 — iOS compatibility and monitored soak

Use the procedure in `oralable_swift/docs/GEN2_COMPATIBILITY_GATE.md` on physical hardware. Record candidate iOS build identity, discovery, version read, pairing, TGM subscriptions, data/reconnect observations, and a defined-duration monitored soak. The iOS app must not claim Gen2 production compatibility until this gate has evidence and the product owner authorizes it.

## Promotion policy

`python3 scripts/verify_gen2_program.py --release-gate` returns nonzero while any gate is pending or the program’s `release_eligible` flag is false. On 16 Nov 2026 the flag was turned on for the 2.0.6 candidate. That does not tag a release, open the kits, or make the iOS app production compatible.

Promotion requires every required gate, the cross-repository TGM checks, an independently green Gen1 release path, reviewer sign-off, and explicit product-owner authorization for a separate release-candidate process. Do not tag a Gen2 build as released, publish an artifact, or update customer-facing compatibility/support copy merely because a candidate compiles.

## Evidence handling

Keep raw captures, measurements, update/recovery observations, and test logs in the approved evidence store rather than embedding personal or device-identifying data in source files. Reference immutable file hashes or controlled record IDs from a reviewed gate update. G2-P2, G2-P3, and G2-P4 record the reviewed 15 Nov 2026 logs on `2.0.6-gen2-nrfconnect`, commit `42a4605`. G2-P6 records the reviewed 16 Nov 2026 phone log on that same image. On 16 Nov 2026 John authorized a release-candidate process and the release flag was turned on. The TGM contract check and the Gen 1 workspace check both passed that day. This is not a product release, a kit release, or a production-compatibility claim.
