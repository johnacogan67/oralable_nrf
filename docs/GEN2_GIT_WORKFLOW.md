# Gen2 Git and Evidence Workflow

**Status:** Gen2 (`pcb00003_gen2` / nRF54L15) is an evidence-gated, non-release program. It remains separate from Gen1 release claims until every gate in [GEN2_PROGRAM_GATE.md](./GEN2_PROGRAM_GATE.md) is independently evidenced and approved.

## Repository and branch policy

Keep the multi-board implementation in `oralable_nrf`; do not fork the firmware, iOS, Core, or Python repositories for Gen2. The working branch may be `feature/gen2-nrf54l15`, but a branch name, build, compile result, or tag has no hardware or product meaning by itself.

The Gen1 release path remains `app/` on `pcb00003/nrf52832` under the repository-pinned NCS v2.7.0 workspace. Gen2 has two non-release targets under a separately initialized NCS v2.9.3 workspace:

- `bringup_ble/` is a bench-only advertiser and marker service.
- `app/` is a Gen2 patient candidate used only to collect controlled compatibility evidence.

Both targets require `--ncs-root` and `--toolchain` arguments. Neither build helper programs a device, creates a release/OTA artifact, or promotes a compatibility claim.

## Change and merge rules

1. Keep the Gen1 build and release-artifact validation independently green; Gen2 changes cannot weaken or redirect that path.
2. Run `python3 scripts/verify_gen2_program.py` after any Gen2 control, manifest, helper, board, or documentation change.
3. For a candidate build, run the matching `verify_firmware_workspace.py --track` command with the separate NCS root and exact toolchain.
4. Review candidate source changes against the versioned TGM GATT contract. The bench advertiser is not evidence of TGM GATT parity.
5. Attach evidence only through a reviewed update to `gen2.program.json`; never convert a build log or an untracked hex into a passed gate.
6. Do not tag or advertise a Gen2 build as released. A `v2.0.0-gen2-g2pX` tag, if used, marks a reviewed evidence snapshot and must state its gate scope, source revision, and non-release status.

## Gate progression

The required gates cover BLE identity and GATT inventory, PPG/accelerometer stream parity, battery/charge/LED behavior, and physical-device iOS compatibility with a monitored soak. The canonical requirements and evidence handling are in [GEN2_PROGRAM_GATE.md](./GEN2_PROGRAM_GATE.md).

`python3 scripts/verify_gen2_program.py --release-gate` is designed to return nonzero today. It may return success only after all evidence gates are passed, cross-repository checks are green, Gen1 remains green, reviewers sign off, and the product owner authorizes a distinct release-candidate process.

## Cross-repository boundary

The firmware source and board files live here; the TGM wire contract is owned by `cursor_oralable/contracts/tgm_gatt_v1.json`; the physical iOS procedure is [GEN2_COMPATIBILITY_GATE.md](../../oralable_swift/docs/GEN2_COMPATIBILITY_GATE.md); and the portfolio tracker is [GEN1_GEN2_TRACKING.md](../../cursor_oralable/docs/GEN1_GEN2_TRACKING.md). Keep their status language consistent: **future/research/bring-up** is acceptable; **shipping/supported/compatible** is not until the evidence program closes.
