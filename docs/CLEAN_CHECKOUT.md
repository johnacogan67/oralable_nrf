# Clean Checkout and Build Paths

This repository has two deliberately separate hardware tracks. They do not share an application entry point, SDK workspace, release status, or artifact policy.

## Gen1 release candidate — the only release firmware path

| Item | Canonical value |
|---|---|
| Application source | `app/` |
| Board | `pcb00003/nrf52832` |
| SDK manifest | repository `west.yml` |
| NCS revision | `sdk-nrf` `v2.7.0` |
| Build mode | west **sysbuild** with MCUboot |
| Build helper | `./scripts/build_firmware.sh` |
| Required release profile | `app/slot_fit.conf` (`CONFIG_SIZE_OPTIMIZATIONS=y`, debug optimization disabled) |
| Required build output | `merged.hex`, `dfu_application.zip`, `app/zephyr/zephyr.signed.bin` |

From a fresh clone:

```bash
cd oralable_nrf
./scripts/bootstrap_gen1_deps.sh       # initializes west and restores NCS v2.7.0
./scripts/verify_firmware_workspace.py --track gen1_release
./scripts/build_firmware.sh --pristine
```

`build_firmware.sh` accepts only the Gen1 board and `app/` source. It applies the mandatory `app/slot_fit.conf` profile so the signed MCUboot image has space for its trailer, validates the current west workspace before building, and validates/records output hashes and effective Kconfig values afterward in the ignored build directory. It does **not** flash a device.

For hardware work, use the separate SWD helper only after reviewing the generated artifact and completing the applicable test plan:

```bash
./scripts/flash_and_rtt.sh --no-build --build-dir build_pcb00003
```

Do not invoke `west build .`, root-level `CMakeLists.txt`, a historical `build_*` directory, or a Gen2 board as a release process. The root-level `CMakeLists.txt` now fails immediately with this guidance, preventing an accidental build of the obsolete root `src/` target.

## Gen2 — evidence-gated candidate program

Gen2 is not a release build path. It has two explicitly non-release targets on `pcb00003_gen2/nrf54l15/cpuapp`: `bringup_ble/` for a bench advertiser and `app/` for a candidate port. Both require a **separate** NCS v2.9.3 workspace and matching Nordic toolchain. The required SDK commit is `09eb26eab8d23b1589bd44ac2a896276bd3ff353`; the required toolchain directory is `b8efef2ad5`.

```bash
# First time only; initializes a separate SDK workspace and requires network access.
./scripts/bootstrap_gen2_ncs.sh --ncs-root /path/to/ncs-v2.9.3

# Local non-release candidates. Neither helper programs hardware or promotes artifacts.
./scripts/build_gen2_bringup.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5 --pristine
./scripts/build_gen2_app.sh --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5 --pristine
```

The candidate outputs stay in ignored repository-relative build directories by default. Do not copy them to `artifacts/`, call them a release, or infer hardware/GATT/iOS compatibility from compilation. Hardware programming and physical evidence collection are controlled procedures outside these build helpers.

Validate the source-controlled guards with:

```bash
python3 scripts/verify_firmware_workspace.py --track gen2_bringup --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5
python3 scripts/verify_firmware_workspace.py --track gen2_patient_candidate --ncs-root /path/to/ncs-v2.9.3 --toolchain /path/to/b8efef2ad5
python3 scripts/verify_gen2_program.py
```

`verify_gen2_program.py --release-gate` intentionally fails while the evidence gates are pending. See [GEN2_PROGRAM_GATE.md](./GEN2_PROGRAM_GATE.md) for the required pin, sensor, BLE, stream, battery, update, iOS, and soak evidence.

## Tandem validation

`./scripts/tandem_validate.sh` now uses the canonical Gen1 build helper and the actual iOS workspace. It does not append to `docs/compatibility_matrix.md` by default. Use `--record` only after automated checks pass and the required manual device checks are planned/documented.

## Diagnostic commands

```bash
python3 scripts/verify_firmware_workspace.py --track gen1_release
python3 scripts/verify_gen2_program.py
python3 scripts/verify_firmware_artifacts.py --build-dir build_pcb00003
```

The verifier commands are read-only unless `verify_firmware_artifacts.py --write-metadata` is explicitly requested by the Gen1 build helper.
