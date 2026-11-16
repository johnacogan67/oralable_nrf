#!/usr/bin/env python3
"""Validate Gen1 sysbuild output and optionally write a build provenance record."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAINS = json.loads((ROOT / "firmware.toolchains.json").read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_revision(path: Path) -> str | None:
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=path, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def app_version() -> str:
    fields: dict[str, str] = {}
    for line in (ROOT / "app" / "VERSION").read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip()
    return ".".join((fields.get("VERSION_MAJOR", "?"), fields.get("VERSION_MINOR", "?"), fields.get("PATCHLEVEL", "?")))


def kconfig_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip().strip('"')
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            values[line[2:-11]] = "n"
    return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build_pcb00003", help="Repository-relative Gen1 sysbuild directory")
    parser.add_argument("--write-metadata", action="store_true", help="Write oralable_build_manifest.json into the build directory")
    args = parser.parse_args()
    build = (ROOT / args.build_dir).resolve()
    try:
        build.relative_to(ROOT)
    except ValueError:
        print("ERROR build directory must be inside the repository")
        return 1

    if "gen2" in build.name.lower():
        print("ERROR this validator is Gen1-release-only; Gen2 candidates are governed by verify_gen2_program.py")
        return 1

    track = TOOLCHAINS["tracks"]["gen1_release"]
    required = [build / relative for relative in track["required_artifacts"]]
    missing = [str(path.relative_to(ROOT)) for path in required if not path.is_file() or path.stat().st_size == 0]
    if missing:
        for path in missing:
            print(f"ERROR missing or empty build artifact: {path}")
        return 1

    config_path = build / "app" / "zephyr" / ".config"
    if not config_path.is_file():
        print(f"ERROR missing generated Kconfig: {config_path.relative_to(ROOT)}")
        return 1
    config = kconfig_values(config_path)
    required_config = track["required_config"]
    assert isinstance(required_config, dict)
    invalid = {
        key: (expected, config.get(key))
        for key, expected in required_config.items()
        if config.get(key) != expected
    }
    if invalid:
        for key, (expected, actual) in invalid.items():
            print(f"ERROR release config {key}: expected {expected}, found {actual}")
        return 1

    print(f"PASS  Gen1 artifacts present in {build.relative_to(ROOT)}")
    for key, expected in required_config.items():
        print(f"PASS  release config {key}={expected}")
    for path in required:
        print(f"PASS  {path.relative_to(ROOT)} sha256={sha256(path)}")

    if args.write_metadata:
        payload = {
            "schema_version": "1.0.0",
            "created_at": datetime.now(timezone.utc).isoformat(),
            "track": "gen1_release",
            "board": track["board"],
            "application": track["application"],
            "firmware_version": app_version(),
            "source_revision": git_revision(ROOT),
            "sdk_revisions": {
                "nrf": git_revision(ROOT / "nrf"),
                "zephyr": git_revision(ROOT / "zephyr"),
            },
            "artifacts": {str(path.relative_to(build)): sha256(path) for path in required},
        }
        target = build / "oralable_build_manifest.json"
        target.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"PASS  wrote build metadata: {target.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
