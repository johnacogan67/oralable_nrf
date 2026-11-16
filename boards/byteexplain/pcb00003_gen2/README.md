# PCB00003 Gen2 — REV11 / BOM REV9

**Status:** Evidence-gated, non-release nRF54L15 board port. The presence of this board definition, a compiled image, or a historical bench observation does not establish hardware readiness, GATT parity, iOS support, OTA readiness, or a product release claim.

The planned hardware identity is Kaga ES4L15BA1 / Nordic nRF54L15 with the LP270829 cell. The Gen2 source remains a port of the Gen1 application and TGM protocol; it must not create a separate wire contract.

## Build boundary

The repository supports two explicitly non-release local builds, both against a separately initialized NCS v2.9.3 workspace and a matching toolchain:

```sh
./scripts/build_gen2_bringup.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5 --pristine
./scripts/build_gen2_app.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5 --pristine
```

`bringup_ble/` is a bench advertiser only: it drives BATEN, advertises as `Oralable-G2`, accepts a connection, and returns a marker characteristic. It leaves I²C, ADC, and sensors off; it is not Oralable TGM GATT or a kit image. The `app/` result is a candidate port used only to gather controlled evidence. Neither helper programs hardware, creates an OTA, or writes a release artifact.

## Hardware boundary

The DTS contains the port map for BATEN, SENS_EN, CHRSTS, BATVOL, I²C, and the sensor interrupts. Firmware identity reads are the sensor record. Do not copy Gen1 CG-320B battery/charge/LED thresholds to the LP270829 cell.

Physical programming, reset, and measurement are recorded in the bench note. A release helper does not program the board. A past observation of an `Oralable-G2` advertisement is a historical note only and must be re-captured with the candidate revision, hardware identity, and reviewer evidence before it can close a gate.

## Required gates and references

Start with [GEN2_PROGRAM_GATE.md](../../../docs/GEN2_PROGRAM_GATE.md). It defines the required gates and the evidence for BLE/TGM behavior, streams, battery/charging/LEDs, iOS compatibility, and soak testing.

Related references:

- [firmware.toolchains.json](../../../firmware.toolchains.json) — exact NCS/toolchain tracks.
- [GEN2_GIT_WORKFLOW.md](../../../docs/GEN2_GIT_WORKFLOW.md) — source-control and promotion rules.
- [GEN1_GEN2_TRACKING.md](../../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md) — portfolio tracker.
- [GEN2_COMPATIBILITY_GATE.md](../../../oralable_swift/docs/GEN2_COMPATIBILITY_GATE.md) — physical iOS evidence procedure.

The only appropriate status before complete evidence is **gated bring-up**. Do not describe this board as release-ready, customer-compatible, or supported.
