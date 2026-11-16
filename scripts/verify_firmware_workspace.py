#!/usr/bin/env python3
"""Verify Oralable firmware workspace prerequisites without building or flashing."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "firmware.toolchains.json"
GEN2_TRACKS = ("gen2_bringup", "gen2_patient_candidate")


def run(*args: str, cwd: Path | None = None) -> str | None:
    try:
        return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def git_head(path: Path) -> str | None:
    return run("git", "rev-parse", "HEAD", cwd=path)


def git_ref(path: Path, ref: str) -> str | None:
    return run("git", "rev-parse", f"{ref}^{{commit}}", cwd=path)


def check_exact_revision(errors: list[str], label: str, path: Path, revision: str) -> None:
    if not path.is_dir():
        errors.append(f"{label}: missing SDK checkout at {path}")
        return
    expected = git_ref(path, revision)
    actual = git_head(path)
    if expected is None:
        errors.append(f"{label}: required revision {revision} is unavailable in {path}")
    elif actual != expected:
        errors.append(f"{label}: expected {revision} ({expected}), found {actual}")


def verify_gen1(track: dict[str, Any]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    passes: list[str] = []
    app = ROOT / str(track["application"])
    board = ROOT / "boards" / "byteexplain" / "pcb00003" / "board.yml"
    if not (app / "CMakeLists.txt").is_file() or not (app / "prj.conf").is_file():
        errors.append("Gen1 release: app/ is missing CMakeLists.txt or prj.conf")
    else:
        passes.append("Gen1 release app entry point: app/")
    if not board.is_file():
        errors.append(f"Gen1 release: board definition missing: {board}")
    else:
        passes.append(f"Gen1 release board: {track['board']}")
    west_manifest = ROOT / str(track["west_manifest"])
    if not west_manifest.is_file() or "revision: v2.7.0" not in west_manifest.read_text(encoding="utf-8"):
        errors.append("Gen1 release: west.yml does not pin NCS sdk-nrf v2.7.0")
    else:
        passes.append("Gen1 release west manifest pins sdk-nrf v2.7.0")
    topdir = run("west", "topdir", cwd=ROOT)
    if topdir != str(ROOT):
        errors.append("Gen1 release: initialize this checkout with `west init -l .` then `west update`")
    else:
        passes.append("Gen1 west workspace root matches repository")
    ncs = track["ncs"]
    assert isinstance(ncs, dict)
    check_exact_revision(errors, "Gen1 sdk-nrf", ROOT / "nrf", str(ncs["sdk_nrf_revision"]))
    check_exact_revision(errors, "Gen1 Zephyr", ROOT / "zephyr", str(ncs["zephyr_revision"]))
    if not errors:
        passes.append("Gen1 SDK revisions match the release manifest")
    return passes, errors


def verify_gen2(track_name: str, track: dict[str, Any], ncs_root: Path | None, toolchain: Path | None) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    passes: list[str] = []
    label = "Gen2 bench advertiser" if track_name == "gen2_bringup" else "Gen2 patient candidate"
    if ncs_root is None:
        return passes, [f"{label}: --ncs-root is required (a separately initialized NCS v2.9.3 workspace)"]
    if toolchain is None:
        return passes, [f"{label}: --toolchain is required (the pinned Nordic NCS toolchain)"]
    ncs_root = ncs_root.resolve()
    toolchain = toolchain.resolve()
    app = ROOT / str(track["application"])
    board = ROOT / "boards" / "byteexplain" / "pcb00003_gen2" / "board.yml"
    if not (app / "CMakeLists.txt").is_file() or not (app / "prj.conf").is_file():
        errors.append(f"{label}: {track['application']}/ is missing CMakeLists.txt or prj.conf")
    else:
        passes.append(f"{label} entry point: {track['application']}/")
    if not board.is_file():
        errors.append(f"{label}: board definition missing: {board}")
    else:
        passes.append(f"{label} board: {track['board']}")

    ncs = track["ncs"]
    assert isinstance(ncs, dict)
    nrf = ncs_root / "nrf"
    zephyr = ncs_root / "zephyr"
    expected_nrf = str(ncs["sdk_nrf_commit"])
    # The pin may be an annotated tag object (v2.9.3). Compare the commit it names.
    expected_commit = git_ref(nrf, expected_nrf)
    actual_nrf = git_head(nrf)
    if expected_commit is None:
        errors.append(f"{label} sdk-nrf: required revision {expected_nrf} is unavailable in {nrf}")
    elif actual_nrf != expected_commit:
        errors.append(f"{label} sdk-nrf: expected {expected_nrf} ({expected_commit}), found {actual_nrf}")
    else:
        passes.append(f"{label} sdk-nrf matches NCS v2.9.3 manifest commit")
    check_exact_revision(errors, f"{label} Zephyr", zephyr, str(ncs["zephyr_revision"]))

    toolchain_info = track["toolchain"]
    assert isinstance(toolchain_info, dict)
    expected_toolchain = str(toolchain_info["ncs_toolchain_id"])
    if toolchain.name != expected_toolchain:
        errors.append(f"{label} toolchain: expected directory name {expected_toolchain}, found {toolchain.name}")
    elif not (toolchain / "bin" / "west").is_file() or not (toolchain / "bin" / "ninja").is_file() or not (toolchain / "opt" / "zephyr-sdk").is_dir():
        errors.append(f"{label} toolchain: missing required west, ninja, or Zephyr SDK under {toolchain}")
    else:
        passes.append(f"{label} toolchain: {expected_toolchain}")
    if not errors:
        passes.append(f"{label} workspace: {ncs_root}")
    return passes, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--track", choices=("gen1_release", *GEN2_TRACKS), default="gen1_release")
    parser.add_argument("--ncs-root", type=Path, help="Separate NCS workspace required for Gen2 tracks")
    parser.add_argument("--toolchain", type=Path, help="Pinned Nordic NCS toolchain required for Gen2 tracks")
    parser.add_argument("--json", action="store_true", help="Emit a machine-readable report")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    track = manifest["tracks"][args.track]
    assert isinstance(track, dict)
    passes, errors = verify_gen1(track) if args.track == "gen1_release" else verify_gen2(args.track, track, args.ncs_root, args.toolchain)
    report = {"track": args.track, "passes": passes, "errors": errors}
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"Firmware track: {args.track}")
        for item in passes:
            print(f"PASS  {item}")
        for item in errors:
            print(f"ERROR {item}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
