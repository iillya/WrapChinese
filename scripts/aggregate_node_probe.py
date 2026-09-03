#!/usr/bin/env python3
"""Subtract the common Wrap UI baseline from per-node runtime captures."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_json(path: Path) -> dict[str, object]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"Expected an object: {path}")
    return value


def capture_keys(path: Path) -> set[str]:
    document = load_json(path)
    translations = document.get("translations")
    if document.get("$schema") != "sp-translation-v1" or not isinstance(translations, dict):
        raise ValueError(f"Invalid capture file: {path}")
    return {
        key for key, value in translations.items()
        if isinstance(key, str) and key.strip() and isinstance(value, str)
    }


def aggregate(manifest_path: Path) -> dict[str, object]:
    manifest = load_json(manifest_path)
    if manifest.get("schema") != "wrap-node-probe-manifest-v1":
        raise ValueError("Unsupported node probe manifest")
    baseline_info = manifest.get("baseline")
    nodes = manifest.get("nodes")
    if not isinstance(baseline_info, dict) or not isinstance(nodes, list):
        raise ValueError("Incomplete node probe manifest")
    baseline = capture_keys(Path(str(baseline_info["capture"])))

    node_results: dict[str, object] = {}
    all_candidates: set[str] = set()
    missing: list[str] = []
    for item in nodes:
        if not isinstance(item, dict):
            raise ValueError("Invalid node entry in manifest")
        node_type = str(item.get("node_type", ""))
        capture = Path(str(item.get("capture", "")))
        if not capture.is_file():
            missing.append(node_type)
            continue
        candidates = sorted(capture_keys(capture) - baseline, key=str.casefold)
        all_candidates.update(candidates)
        node_results[node_type] = {
            "candidate_count": len(candidates),
            "candidates": candidates,
        }
    return {
        "schema": "wrap-node-parameter-inventory-v1",
        "wrap_version": manifest.get("wrap_version"),
        "format_version": manifest.get("format_version"),
        "baseline_count": len(baseline),
        "completed_node_count": len(node_results),
        "missing_nodes": missing,
        "unique_candidate_count": len(all_candidates),
        "unique_candidates": sorted(all_candidates, key=str.casefold),
        "nodes": node_results,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--manifest", type=Path,
        default=ROOT / "build" / "node_probe" / "manifest.json",
    )
    parser.add_argument(
        "--output", type=Path,
        default=ROOT / "translations" / "inventory" / "node_parameter_runtime_probe.json",
    )
    args = parser.parse_args()
    result = aggregate(args.manifest.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    temporary.replace(args.output)
    print(
        f"nodes={result['completed_node_count']} "
        f"missing={len(result['missing_nodes'])} "
        f"unique_candidates={result['unique_candidate_count']}"
    )
    print(f"output={args.output.resolve()}")
    return 0 if not result["missing_nodes"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
