#!/usr/bin/env python3
"""Refuse a flash image that keeps the version of the last flashed image.

Ordinary compiles are not checked. The Gen1 and Gen2 flash-image scripts run
this when they produce a hex. GATT 006 and Device Manager both use the number.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

TRACKS = {
    "gen2": {
        "stamp": ROOT / "gen2_image_stamp.txt",
        "version_file": ROOT / "app/boards/pcb00003_gen2_nrf54l15_cpuapp.conf",
        "version_pattern": re.compile(
            r'CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="(\d+)\.(\d+)\.(\d+)\+(\d+)"'
        ),
        "inputs": [
            "app/src",
            "app/prj.conf",
            "app/debug.conf",
            "app/CMakeLists.txt",
            "app/cmake",
            "app/Kconfig",
            "app/sysbuild",
            "app/sysbuild.conf",
            "app/sysbuild.cmake",
            "app/boards/pcb00003_gen2_nrf54l15_cpuapp.conf",
            "boards/byteexplain/pcb00003_gen2",
            "include",
            "module",
        ],
    },
    "gen1": {
        "stamp": ROOT / "gen1_image_stamp.txt",
        "version_file": ROOT / "app/VERSION",
        "version_pattern": None,
        "inputs": [
            "app/src",
            "app/prj.conf",
            "app/debug.conf",
            "app/slot_fit.conf",
            "app/VERSION",
            "app/CMakeLists.txt",
            "app/cmake",
            "app/Kconfig",
            "app/sysbuild",
            "app/sysbuild.conf",
            "app/sysbuild.cmake",
            "boards/byteexplain/pcb00003",
            "include",
            "module",
        ],
    },
}


def parse_version(text: str, pattern: re.Pattern[str] | None) -> tuple[int, int, int, int]:
    if pattern is None:
        fields = {}
        for line in text.splitlines():
            if "=" not in line or line.strip().startswith("#"):
                continue
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip()
        return (
            int(fields["VERSION_MAJOR"]),
            int(fields["VERSION_MINOR"]),
            int(fields["PATCHLEVEL"]),
            int(fields.get("VERSION_TWEAK") or "0"),
        )
    match = pattern.search(text)
    if match is None:
        raise SystemExit(f"ERROR: sign version is missing from the firmware config")
    return tuple(int(part) for part in match.groups())  # type: ignore[return-value]


def version_text(version: tuple[int, int, int, int]) -> str:
    major, minor, patch, tweak = version
    return f"{major}.{minor}.{patch}+{tweak}"


def file_hash(inputs: list[str]) -> str:
    digest = hashlib.sha256()
    paths: list[Path] = []
    for item in inputs:
        path = ROOT / item
        if not path.exists():
            raise SystemExit(f"ERROR: firmware input missing: {path}")
        if path.is_file():
            paths.append(path)
            continue
        for child in path.rglob("*"):
            if not child.is_file():
                continue
            if child.suffix == ".md" or child.name == ".DS_Store" or "__pycache__" in child.parts:
                continue
            paths.append(child)
    for path in sorted(paths, key=lambda p: p.relative_to(ROOT).as_posix()):
        rel = path.relative_to(ROOT).as_posix().encode()
        digest.update(rel)
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def read_stamp(path: Path) -> tuple[tuple[int, int, int, int], str] | None:
    if not path.is_file():
        return None
    version: tuple[int, int, int, int] | None = None
    digest = ""
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("version="):
            text = line.split("=", 1)[1]
            match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)\+(\d+)", text.strip())
            if match is None:
                raise SystemExit(f"ERROR: bad version in {path}")
            version = tuple(int(part) for part in match.groups())  # type: ignore[assignment]
        elif line.startswith("sha256="):
            digest = line.split("=", 1)[1].strip()
    if version is None or len(digest) != 64:
        raise SystemExit(f"ERROR: incomplete image stamp {path}")
    return version, digest


def write_stamp(path: Path, version: tuple[int, int, int, int], digest: str) -> None:
    path.write_text(
        "version=" + version_text(version) + "\n" + "sha256=" + digest + "\n",
        encoding="utf-8",
    )


def load(track_name: str) -> tuple[dict, tuple[int, int, int, int], str]:
    track = TRACKS[track_name]
    text = Path(track["version_file"]).read_text(encoding="utf-8")
    version = parse_version(text, track["version_pattern"])
    digest = file_hash(track["inputs"])
    return track, version, digest


def check(track_name: str) -> int:
    track, version, digest = load(track_name)
    stamp = read_stamp(track["stamp"])
    if stamp is None:
        print(f"{track_name}: no recorded image yet. This build will record {version_text(version)}.")
        return 0
    previous, previous_hash = stamp
    if digest == previous_hash:
        print(f"{track_name}: image inputs match {version_text(previous)}.")
        return 0
    if version <= previous:
        print(
            f"ERROR: this flash image differs from {version_text(previous)} but the version is still {version_text(version)}.",
            file=sys.stderr,
        )
        print(
            "ERROR: raise the version before flashing so GATT 006 and Device Manager can tell the images apart.",
            file=sys.stderr,
        )
        return 1
    print(f"{track_name}: image version {version_text(version)} is newer than {version_text(previous)}.")
    return 0


def record(track_name: str) -> int:
    track, version, digest = load(track_name)
    if check(track_name) != 0:
        return 1
    write_stamp(track["stamp"], version, digest)
    print(f"{track_name}: recorded image {version_text(version)}.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("check", "record"))
    parser.add_argument("track", choices=tuple(TRACKS))
    args = parser.parse_args()
    return check(args.track) if args.action == "check" else record(args.track)


if __name__ == "__main__":
    sys.exit(main())
