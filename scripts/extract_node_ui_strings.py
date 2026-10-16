#!/usr/bin/env python3
"""Extract review candidates from Wrap node-definition string tables.

Unlike extract_wrap_strings.py, this scanner deliberately keeps short enum
values (for example ``Clamp`` and ``Bicubic``).  Confirmed parameter
label/internal-key pairs are used as positional anchors; candidates are never
written directly to the release dictionary.
"""

from __future__ import annotations

import argparse
import bisect
import json
import os
import re
from collections import defaultdict
from pathlib import Path


ASCII_NUL = re.compile(rb"[\x20-\x7e]{2,}\x00")
WORDS = re.compile(r"[A-Za-z]{2,}")
LOWER_KEY = re.compile(r"^[a-z_][A-Za-z0-9_]*$")
MANGLED = re.compile(
    r"(?:^_?Z|^\?|::|^F\d|^P?FN?\d|^N\d|^St\d|QVector<|QtPrivate|QMetaType|"
    r"ProgressNotifier|Lock_policy|shared_ptr|optional<)"
)
FILEISH = re.compile(
    r"(?:[/\\]|^:\/|^qrc:|https?://|\*\.|\.(?:png|jpg|obj|fbx|json|py|dll|exe|h|hpp)$)",
    re.IGNORECASE,
)
CODEISH = re.compile(r"(?:[{};]|\b(?:std|const|void|float|double)::|->|==|!=)")
SHORTCUT = re.compile(
    r"^(?:(?:Ctrl|Alt|Shift)(?:\+(?:Ctrl|Alt|Shift))?\+?[^ ]+|F\d{1,2}|"
    r"Home|End|Del|(?:,|\.)\s*/\s*Shift\+(?:Left|Right))"
    r"(?:\s*/\s*(?:Ctrl|Alt|Shift|F\d)[^ ]*)?$"
)


def load_translations(path: Path) -> dict[str, str]:
    return json.loads(path.read_text(encoding="utf-8"))["translations"]


def plausible_ui(text: str, internal_keys: set[str]) -> tuple[bool, str]:
    text = text.strip()
    if not (2 <= len(text) <= 360) or not WORDS.search(text):
        return False, "shape"
    if text in internal_keys or LOWER_KEY.fullmatch(text):
        return False, "internal-key"
    if SHORTCUT.fullmatch(text):
        return False, "shortcut"
    if MANGLED.search(text) or FILEISH.search(text) or CODEISH.search(text):
        return False, "code-or-path"
    if text.count("?") > 2 or text.count("@") > 1:
        return False, "binary-noise"
    # UI strings normally start with a capital, a placeholder, or simple
    # punctuation used by a formatted status message.
    if not (text[0].isupper() or text.startswith("%") or text.startswith("<")):
        return False, "lowercase-fragment"
    return True, "anchored-node-string"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "exe", nargs="?", type=Path,
        default=Path(os.environ.get("WRAP_ROOT", r"C:\Program Files\Faceform\Wrap 2025.11.14")) / "Wrap.exe",
    )
    parser.add_argument(
        "--anchors", type=Path,
        default=Path(__file__).resolve().parents[1] / "translations" / "inventory"
        / "node_parameter_static_missing.json",
    )
    parser.add_argument(
        "--dictionary", type=Path,
        default=Path(__file__).resolve().parents[1] / "translations" / "dictionary_zh.json",
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path(__file__).resolve().parents[1] / "translations" / "inventory"
        / "node_ui_extended_missing.json",
    )
    parser.add_argument("--window", type=int, default=768)
    args = parser.parse_args()

    anchors_doc = json.loads(args.anchors.read_text(encoding="utf-8"))
    anchors = anchors_doc["candidates"]
    anchor_offsets = sorted(int(item["offset"]) for item in anchors)
    anchor_by_offset = {int(item["offset"]): str(item["source"]) for item in anchors}
    internal_keys = {
        str(key)
        for item in anchors
        for key in item.get("parameterKeyExamples", [])
    }
    dictionary = load_translations(args.dictionary)
    data = args.exe.read_bytes()

    found: dict[str, dict[str, object]] = {}
    rejected = defaultdict(int)
    for match in ASCII_NUL.finditer(data):
        offset = match.start()
        pos = bisect.bisect_left(anchor_offsets, offset)
        neighbours = anchor_offsets[max(0, pos - 1): min(len(anchor_offsets), pos + 2)]
        if not neighbours:
            continue
        nearest = min(neighbours, key=lambda value: abs(value - offset))
        distance = abs(nearest - offset)
        if distance > args.window:
            continue
        text = match.group()[:-1].decode("ascii", "ignore").strip()
        ok, reason = plausible_ui(text, internal_keys)
        if not ok:
            rejected[reason] += 1
            continue
        if text in dictionary:
            continue
        evidence = {
            "source": text,
            "offset": offset,
            "nearestParameter": anchor_by_offset[nearest],
            "distance": distance,
            "confidence": "strong" if distance <= 256 else "contextual",
            "reason": reason,
        }
        previous = found.get(text)
        if previous is None or distance < int(previous["distance"]):
            found[text] = evidence

    candidates = sorted(
        found.values(),
        key=lambda item: (
            0 if item["confidence"] == "strong" else 1,
            int(item["offset"]),
            str(item["source"]),
        ),
    )
    confidence_counts = {
        level: sum(item["confidence"] == level for item in candidates)
        for level in ("strong", "contextual")
    }
    payload = {
        "$schema": "wrap-node-ui-extended-inventory-v1",
        "source": str(args.exe),
        "method": "NUL-delimited strings near confirmed node parameter label/key anchors",
        "windowBytes": args.window,
        "anchorCount": len(anchors),
        "candidateCount": len(candidates),
        "confidenceCounts": confidence_counts,
        "rejectedReasonCounts": dict(sorted(rejected.items())),
        "candidates": candidates,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(candidates)} candidates to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
