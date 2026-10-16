"""Audit reviewed Wrap help lines against a local executable without running it."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
KEY = re.compile(r"\*(?:LMB|RMB|MMB|SHIFT|CTRL|ALT|SPACE|ESC|ENTER|[A-Z0-9\[\]<>])\*")
KEY_TOKEN = re.compile(r"\*([^*]+)\*")
START = re.compile(r"(?:Hold |Release )?\*(?:LMB|RMB|MMB|SHIFT|CTRL|ALT|SPACE|ESC|ENTER|[A-Z0-9\[\]<>])\*")


def extract(data: bytes) -> dict[str, list[int]]:
    found: dict[str, list[int]] = {}
    for match in re.finditer(rb"[\x09\x0a\x0d\x20-\x7e]{8,}\x00", data):
        cursor = match.start()
        for raw in match.group()[:-1].splitlines(keepends=True):
            line = raw.decode("ascii").rstrip("\r\n")
            # A preceding IEEE double can end in ASCII '?' immediately before
            # a C-string. Locate the explicit key syntax, not that binary byte.
            begin = START.search(line)
            if begin and len(line) < 200 and not any(
                token in line for token in ("gl_", "vec4", "texture2D")
            ):
                source = line[begin.start():]
                if KEY.search(source):
                    found.setdefault(source, []).append(cursor + begin.start())
            cursor += len(raw)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/shortcut-hints-audit.json")
    args = parser.parse_args()
    if args.exe.stat().st_size > 256 * 1024 * 1024:
        parser.error("executable exceeds the 256 MiB static scan limit")
    data = args.exe.read_bytes()
    dictionary = (ROOT / "translations/dictionary_zh.json").read_bytes()
    translations = json.loads(dictionary)["translations"]
    review = json.loads((ROOT / "translations/review/shortcut_hints_20261016_zh.review.json")
                        .read_text(encoding="utf-8"))
    approved = {row["source"] for row in review["hints"]}
    header = (ROOT / "source/shortcut_hint_sources.h").read_text(encoding="utf-8")
    runtime = {json.loads(line.strip().rstrip(",")) for line in header.splitlines()
               if line.strip().startswith('"')}
    found = extract(data)
    errors: list[str] = []
    for source in found:
        target = translations.get(source, "")
        if source not in approved or source not in runtime or not target:
            errors.append("uncovered help: " + source)
        elif KEY_TOKEN.findall(source) != KEY_TOKEN.findall(target):
            errors.append("key sequence changed: " + source)
    tooltips = sorted({m.group().decode("ascii") for m in re.finditer(rb"[\x20-\x7e]{8,}", data)
                       if b"Shortcut:" in m.group()})
    for source in tooltips:
        if not translations.get(source): errors.append("uncovered tooltip: " + source)
    if approved != runtime: errors.append("review and runtime allowlist differ")
    result = {"schema": "wrap-shortcut-audit-v1", "host": str(args.exe.resolve()),
              "host_sha256": hashlib.sha256(data).hexdigest(),
              "dictionary_sha256": hashlib.sha256(dictionary).hexdigest(),
              "hint_count": len(found), "tooltip_count": len(tooltips),
              "errors": errors, "sources": found}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"shortcut_hints={len(found)} tooltips={len(tooltips)} errors={len(errors)}")
    for error in errors: print(error)
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
