#!/usr/bin/env python3
"""Validate the release dictionary and report simple quality statistics."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import re
import unicodedata
from pathlib import Path


CJK = re.compile(r"[\u3400-\u9fff\uf900-\ufaff]")
PLACEHOLDER = re.compile(r"%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+|\.\*)?[hlLjzt]*[diuoxXfFeEgGaAcspn%]")
QT_PLACEHOLDER = re.compile(r"%(?:L?[1-9]\d?|n)")
HTML_TAG = re.compile(r"</?[A-Za-z][^>]*>")
MAXIMUM_FILE_BYTES = 32 * 1024 * 1024
MAXIMUM_ENTRIES = 200_000
MAXIMUM_SOURCE_LENGTH = 4096
MAXIMUM_TARGET_LENGTH = 8192


class DuplicateJsonKey(ValueError):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateJsonKey(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def normalized_key(text: str) -> str:
    text = text.replace("\u00a0", " ").replace("\u3000", " ")
    text = text.replace("\u200b", "").replace("\u200d", "").replace("\ufeff", "")
    text = " ".join(text.split())
    while text.endswith("…"):
        text = text[:-1]
    while text.endswith("..."):
        text = text[:-3]
    if text.endswith("*"):
        text = text[:-1]
    mnemonic_free = []
    index = 0
    while index < len(text):
        if text[index] != "&":
            mnemonic_free.append(text[index])
        elif index + 1 < len(text) and text[index + 1] == "&":
            mnemonic_free.append("&")
            index += 1
        index += 1
    return unicodedata.normalize("NFC", "".join(mnemonic_free).casefold()).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "dictionary", nargs="?", type=Path,
        default=Path(__file__).resolve().parents[1] / "translations" / "dictionary_zh.json",
    )
    args = parser.parse_args()
    try:
        if args.dictionary.stat().st_size > MAXIMUM_FILE_BYTES:
            raise ValueError("dictionary exceeds runtime size limit")
        doc = json.loads(args.dictionary.read_text(encoding="utf-8"),
                         object_pairs_hook=unique_object)
    except (OSError, json.JSONDecodeError, DuplicateJsonKey, ValueError) as error:
        print(f"ERROR: {error}")
        return 1
    if not isinstance(doc, dict):
        print("ERROR: dictionary root must be an object")
        return 1
    translations = doc.get("translations")
    if (doc.get("$schema") != "sp-translation-v1" or
            doc.get("language") != "zh-CN" or not isinstance(doc.get("id"), str) or
            not doc["id"].strip() or not isinstance(translations, dict)):
        print("ERROR: expected a named zh-CN sp-translation-v1 dictionary")
        return 1
    errors: list[str] = []
    if len(translations) > MAXIMUM_ENTRIES:
        errors.append("dictionary exceeds runtime entry limit")
    values: dict[str, str] = {}
    normalized: dict[str, tuple[str, str]] = {}
    for source, target in translations.items():
        if not isinstance(source, str) or not source.strip():
            errors.append("empty or non-string source key")
            continue
        if not isinstance(target, str) or not target.strip():
            errors.append(f"empty translation: {source!r}")
            continue
        if len(source) > MAXIMUM_SOURCE_LENGTH or len(target) > MAXIMUM_TARGET_LENGTH:
            errors.append(f"entry exceeds runtime length limit: {source!r}")
        if any(ord(character) < 0x20 and character not in "\t\r\n"
               for character in source + target):
            errors.append(f"unsupported control character: {source!r}")
        if not CJK.search(target) and target.casefold() != source.casefold():
            errors.append(f"translation has no CJK: {source!r} -> {target!r}")
        source_placeholders = Counter(PLACEHOLDER.findall(source))
        target_placeholders = Counter(PLACEHOLDER.findall(target))
        source_qt_placeholders = Counter(QT_PLACEHOLDER.findall(source))
        target_qt_placeholders = Counter(QT_PLACEHOLDER.findall(target))
        if (source_placeholders != target_placeholders or
                source_qt_placeholders != target_qt_placeholders):
            errors.append(f"placeholder mismatch: {source!r} -> {target!r}")
        if Counter(tag.casefold() for tag in HTML_TAG.findall(source)) != Counter(
                tag.casefold() for tag in HTML_TAG.findall(target)):
            errors.append(f"HTML tag mismatch: {source!r} -> {target!r}")
        folded = normalized_key(source)
        prior_normalized = normalized.get(folded)
        if prior_normalized and prior_normalized[1] != target:
            errors.append(
                f"ambiguous normalized keys: {prior_normalized[0]!r} and {source!r}")
        else:
            normalized[folded] = (source, target)
        prior = values.get(target)
        if prior and prior != source and len(target) > 2:
            # Duplicates are not invalid, but they are useful during review.
            pass
        values[target] = source
    print(f"entries={len(translations)} errors={len(errors)}")
    for error in errors[:100]:
        print(f"ERROR: {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
