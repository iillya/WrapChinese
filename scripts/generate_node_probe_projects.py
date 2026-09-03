#!/usr/bin/env python3
"""Generate isolated Wrap projects for no-input runtime parameter probing."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WRAP_CMD = Path(r"C:\Program Files\Faceform\Wrap 2025.10.8\WrapCmd.exe")
NODE_TYPE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def run_wrap_cmd(executable: Path, option: str) -> str:
    completed = subprocess.run(
        [str(executable), option],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="strict",
        timeout=30,
    )
    return completed.stdout.strip()


def project_document(format_version: int) -> dict[str, object]:
    return {
        "commonDir": "",
        "formatVersion": format_version,
        "nodes": {},
        "pythonCode": "",
        "settings": {
            "fps": 60,
            "mustBeCopiedFromSettings": False,
            "playbackRepeat": False,
            "playbackSkip": False,
            "playbackStep": 1,
            "playbackVolume": 1.0,
            "syncAudioSec": 0.0,
            "syncTimelineFrame": 0,
        },
        "timeline": {"current": 0, "max": 0, "min": 0},
    }


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(path)


def generate(wrap_cmd: Path, output: Path) -> dict[str, object]:
    if not wrap_cmd.is_file():
        raise FileNotFoundError(f"WrapCmd not found: {wrap_cmd}")
    format_text = run_wrap_cmd(wrap_cmd, "--format")
    if not format_text.isdigit():
        raise ValueError(f"Unexpected project format: {format_text!r}")
    format_version = int(format_text)
    node_types = [line.strip() for line in run_wrap_cmd(wrap_cmd, "--nodes").splitlines()]
    if not node_types or any(not NODE_TYPE.fullmatch(value) for value in node_types):
        raise ValueError("WrapCmd returned an invalid node type list")
    if len(node_types) != len(set(node_types)):
        raise ValueError("WrapCmd returned duplicate node types")

    projects = output / "projects"
    entries: list[dict[str, str]] = []
    baseline = projects / "000_baseline.wrap"
    write_json(baseline, project_document(format_version))
    for index, node_type in enumerate(node_types, start=1):
        entries.append({
            "node_type": node_type,
            "capture": str((output / "captures" / f"{index:03d}_{node_type}.json").resolve()),
        })

    manifest: dict[str, object] = {
        "schema": "wrap-node-probe-manifest-v1",
        "wrap_cmd": str(wrap_cmd.resolve()),
        "wrap_version": run_wrap_cmd(wrap_cmd, "--version"),
        "format_version": format_version,
        "baseline": {
            "project": str(baseline.resolve()),
            "capture": str((output / "captures" / "000_baseline.json").resolve()),
        },
        "node_count": len(entries),
        "nodes": entries,
    }
    write_json(output / "manifest.json", manifest)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wrap-cmd", type=Path, default=DEFAULT_WRAP_CMD)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "node_probe")
    args = parser.parse_args()
    manifest = generate(args.wrap_cmd, args.output.resolve())
    print(
        f"generated one baseline project + {manifest['node_count']} node jobs "
        f"for format {manifest['format_version']}"
    )
    print(f"manifest={args.output.resolve() / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
