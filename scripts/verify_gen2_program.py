#!/usr/bin/env python3
"""Verify Gen2 program controls without building, flashing, or changing a hardware state."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
PROGRAM_PATH = ROOT / "gen2.program.json"
TOOLCHAINS_PATH = ROOT / "firmware.toolchains.json"
RESEARCH = ROOT.parent / "cursor_oralable"
IOS = ROOT.parent / "oralable_swift"

EXPECTED_GATES = ("G2-P2", "G2-P3", "G2-P4", "G2-P6")
ALLOWED_STATUSES = {"pending", "in_progress", "passed"}
FORBIDDEN_PROGRAM_COMMANDS = (
    r"\bwest\s+flash\b",
    r"\bnrfutil\s+device\s+program\b",
    r"\bnrfjprog\b",
)
EMIT_PROGRESS = True


def require(condition: bool, message: str, failures: list[str]) -> None:
    if condition and EMIT_PROGRESS:
        print(f"PASS  {message}")
    elif not condition:
        print(f"ERROR {message}")
        failures.append(message)


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def required_text(path: Path) -> str:
    if not path.is_file():
        raise FileNotFoundError(path)
    return path.read_text(encoding="utf-8")


def has_no_programming_command(source: str) -> bool:
    return not any(re.search(pattern, source, re.IGNORECASE) for pattern in FORBIDDEN_PROGRAM_COMMANDS)


def validate_program(program: dict[str, Any], toolchains: dict[str, Any]) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    pending: list[str] = []

    authorization = program.get("release_candidate_authorization")
    authorized = isinstance(authorization, dict) and authorization.get("authorized") is True
    eligible = program.get("release_eligible") is True and authorized
    closed = program.get("status") == "evidence_gated_non_release" and program.get("release_eligible") is False
    require(
        program.get("status") == "evidence_gated_non_release" and (closed or eligible),
        "Gen2 release flag is on for an authorized candidate" if eligible else "Gen2 program is explicitly non-release",
        failures,
    )
    gates = program.get("required_gates")
    require(isinstance(gates, list), "Gen2 program declares a gate list", failures)
    if not isinstance(gates, list):
        return failures, pending

    gate_ids = tuple(str(gate.get("id", "")) for gate in gates if isinstance(gate, dict))
    require(gate_ids == EXPECTED_GATES, "Gen2 program declares the required gates in order", failures)
    for gate in gates:
        if not isinstance(gate, dict):
            failures.append("Gen2 program contains a non-object gate")
            continue
        gate_id = str(gate.get("id", "unknown"))
        status = gate.get("status")
        evidence = gate.get("evidence_required")
        require(status in ALLOWED_STATUSES, f"{gate_id} uses an allowed evidence status", failures)
        require(isinstance(evidence, list) and len(evidence) >= 2, f"{gate_id} names its required evidence", failures)
        if status != "passed":
            pending.append(gate_id)

    tracks = program.get("tracks")
    firmware_tracks = toolchains.get("tracks")
    require(isinstance(tracks, dict) and isinstance(firmware_tracks, dict), "Gen2 program and firmware track registries are present", failures)
    if not isinstance(tracks, dict) or not isinstance(firmware_tracks, dict):
        return failures, pending

    expected_tracks = {
        "bench_advertiser": ("gen2_bringup", "bringup_ble"),
        "patient_candidate": ("gen2_patient_candidate", "app"),
    }
    for program_track, (firmware_track, application) in expected_tracks.items():
        program_entry = tracks.get(program_track)
        firmware_entry = firmware_tracks.get(firmware_track)
        require(
            isinstance(program_entry, dict)
            and program_entry.get("firmware_track") == firmware_track
            and program_entry.get("application") == application
            and program_entry.get("release_eligible") is False,
            f"{program_track} is an explicitly non-release program track",
            failures,
        )
        require(
            isinstance(firmware_entry, dict)
            and firmware_entry.get("status") == "evidence_gated_non_release"
            and firmware_entry.get("application") == application
            and firmware_entry.get("board") == "pcb00003_gen2/nrf54l15/cpuapp",
            f"{firmware_track} matches the program board and non-release policy",
            failures,
        )

    return failures, pending


def validate_source_controls(failures: list[str]) -> None:
    candidate = required_text(ROOT / "scripts" / "build_gen2_app.sh")
    advertiser = required_text(ROOT / "scripts" / "build_gen2_bringup.sh")
    for label, source, track in (
        ("Gen2 patient candidate helper", candidate, "gen2_patient_candidate"),
        ("Gen2 bench advertiser helper", advertiser, "gen2_bringup"),
    ):
        require(
            "--ncs-root" in source
            and "--toolchain" in source
            and f"--track {track}" in source
            and "NON-RELEASE" in source,
            f"{label} requires a pinned workspace/toolchain and labels output non-release",
            failures,
        )
        require(has_no_programming_command(source), f"{label} contains no programming command", failures)

    require("--sysbuild" in candidate and "app" in candidate, "Gen2 patient helper starts a reproducible sysbuild candidate", failures)
    require(
        "bringup_ble" in advertiser and '"$TOOLCHAIN/bin/west" build' in advertiser,
        "Gen2 bench helper builds only the advertiser target",
        failures,
    )

    program_doc = required_text(ROOT / "docs" / "GEN2_PROGRAM_GATE.md")
    ios_doc = required_text(IOS / "docs" / "GEN2_COMPATIBILITY_GATE.md")
    tracking = required_text(RESEARCH / "docs" / "GEN1_GEN2_TRACKING.md")
    board_doc = required_text(ROOT / "boards" / "byteexplain" / "pcb00003_gen2" / "README.md")
    clean_checkout = required_text(ROOT / "docs" / "CLEAN_CHECKOUT.md")
    firmware_gate = required_text(IOS / "OralableApp" / "OralableApp" / "Utilities" / "FirmwareGate.swift")

    for gate_id in EXPECTED_GATES:
        require(gate_id in program_doc, f"Gen2 operator guide names {gate_id}", failures)
    require("not a release process" in program_doc.lower(), "Gen2 operator guide prohibits release promotion", failures)
    require("not production compatible" in ios_doc.lower(), "iOS guide keeps Gen2 compatibility unproven", failures)
    require("requiresGen2CompatibilityEvidence" in firmware_gate, "iOS firmware gate exposes Gen2 evidence state", failures)

    stale_programming = "flash_and_rtt.sh --board pcb00003_gen2"
    require(stale_programming not in tracking and stale_programming not in board_doc and stale_programming not in clean_checkout, "canonical Gen2 documentation does not route Gen2 through the Gen1 flash helper", failures)


def main() -> int:
    global EMIT_PROGRESS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-gate", action="store_true", help="Return nonzero until every Gen2 evidence gate is passed and promotion is authorized")
    parser.add_argument("--json", action="store_true", help="Emit a machine-readable summary after validation")
    args = parser.parse_args()
    EMIT_PROGRESS = not args.json

    try:
        program = load_json(PROGRAM_PATH)
        toolchains = load_json(TOOLCHAINS_PATH)
        failures, pending = validate_program(program, toolchains)
        validate_source_controls(failures)
    except (FileNotFoundError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR unable to validate Gen2 program: {error}")
        return 1

    report = {
        "program": program.get("program"),
        "status": program.get("status"),
        "release_eligible": program.get("release_eligible"),
        "pending_gates": pending,
        "failures": failures,
    }
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))

    if failures:
        return 1
    if args.release_gate:
        if program.get("release_eligible") is not True or pending:
            reason = ", ".join(pending) if pending else "release_eligible is false"
            print("ERROR Gen2 release gate is intentionally closed: " + reason)
            return 2
        print("PASS  Gen2 release gate is authorized")
    elif not args.json:
        if program.get("release_eligible") is True:
            print("PASS  Gen2 release flag is on; kits and iOS production compatibility stay closed")
        else:
            print("PASS  Gen2 program controls are consistent; release gate remains closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
