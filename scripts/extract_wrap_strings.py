#!/usr/bin/env python3
"""Extract likely user-facing English strings from the Wrap executable.

The extractor is deliberately conservative: it produces a review inventory,
never promotes machine-selected candidates into the release dictionary.
"""

from __future__ import annotations

import argparse
import json
import os
import re
from collections import Counter
from pathlib import Path


ASCII_RUN = re.compile(rb"[\x20-\x7e]{3,}")
WORD = re.compile(r"[A-Za-z]{2,}")
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_:<>~]*$")
FILEISH = re.compile(
    r"(?:[/\\]|\.(?:cpp|hpp|h|c|py|json|xml|png|jpg|jpeg|obj|fbx|abc|exr|"
    r"wrap|dll|exe|pdb|vert|frag|ttf|css|html?|onnx|drc))",
    re.IGNORECASE,
)
PRINTF = re.compile(r"%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+|\.\*)?[hlLjzt]*[diuoxXfFeEgGaAcspn%]")


def score(text: str) -> int:
    """Return a rough UI-likelihood score."""
    if len(text) > 360 or not WORD.search(text):
        return -100
    if text.startswith(("_Z", "?", "qrc:/", ":/", "http://", "https://")):
        return -100
    if FILEISH.search(text) or "@@" in text or "::" in text:
        return -100
    if "{" in text and "}" in text and not PRINTF.search(text):
        return -20
    if IDENTIFIER.fullmatch(text):
        # Single PascalCase identifiers are commonly node names, which are UI.
        if re.search(r"[a-z][A-Z]", text) and 3 <= len(text) <= 36:
            return 2
        return -8
    value = 0
    if " " in text:
        value += 3
    if text[0].isupper():
        value += 1
    if text.endswith((".", ":", "?", "!", "...")):
        value += 1
    if any(token in text.lower() for token in (
        "project", "node", "load", "save", "select", "image", "geometry",
        "texture", "camera", "license", "error", "warning", "help", "view",
        "points", "mesh", "blendshape", "rig", "export", "import", "delete",
        "settings", "layout", "color", "frame", "path", "file", "folder",
        "cancel", "apply", "reset", "close", "open", "compute", "wrap",
    )):
        value += 2
    if sum(ch.isdigit() for ch in text) > max(5, len(text) // 3):
        value -= 3
    if any(ch in text for ch in ("=", "#", "`", "^", "|")):
        value -= 3
    return value


def extract(path: Path, minimum_score: int) -> list[dict[str, object]]:
    data = path.read_bytes()
    counts: Counter[str] = Counter()
    offsets: dict[str, int] = {}
    for match in ASCII_RUN.finditer(data):
        text = match.group().decode("ascii", "ignore").strip()
        if not text:
            continue
        counts[text] += 1
        offsets.setdefault(text, match.start())
    result = []
    for text, count in counts.items():
        likelihood = score(text)
        if likelihood < minimum_score:
            continue
        result.append({
            "source": text,
            "count": count,
            "offset": offsets[text],
            "score": likelihood,
        })
    result.sort(key=lambda item: (-int(item["score"]), str(item["source"]).casefold()))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "exe", nargs="?", type=Path,
        default=Path(os.environ.get("WRAP_ROOT", r"C:\Program Files\Faceform\Wrap 2025.11.14")) / "Wrap.exe",
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path(__file__).resolve().parents[1]
        / "translations" / "inventory" / "wrap_static_ui_inventory.json",
    )
    parser.add_argument("--minimum-score", type=int, default=2)
    args = parser.parse_args()
    if not args.exe.is_file():
        parser.error(f"Wrap executable not found: {args.exe}")
    entries = extract(args.exe, args.minimum_score)
    payload = {
        "schema": "wrap-static-ui-inventory-v1",
        "source": str(args.exe),
        "count": len(entries),
        "entries": entries,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(entries)} candidates to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
