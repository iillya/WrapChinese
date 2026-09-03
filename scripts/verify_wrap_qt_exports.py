"""Verify every MinGW Qt symbol used by the Wrap display and width hooks."""

from pathlib import Path
import argparse
import struct


SYMBOLS = (
    # QPainter display hooks.
    "_ZN8QPainter8drawTextERK5QRectiRK7QStringPS0_",
    "_ZN8QPainter8drawTextERK6QRectFRK7QStringRK11QTextOption",
    "_ZN8QPainter8drawTextERK6QRectFiRK7QStringPS0_",
    "_ZN8QPainter8drawTextERK7QPointFRK7QString",
    "_ZN8QPainter8drawTextERK7QPointFRK7QStringii",
    # QFontMetrics integer layout hooks.
    "_ZNK12QFontMetrics17horizontalAdvanceERK7QStringi",
    "_ZNK12QFontMetrics5widthERK7QStringi",
    "_ZNK12QFontMetrics5widthERK7QStringii",
    "_ZNK12QFontMetrics12boundingRectERK7QString",
    "_ZNK12QFontMetrics12boundingRectERK5QRectiRK7QStringiPi",
    "_ZNK12QFontMetrics4sizeEiRK7QStringiPi",
    "_ZNK12QFontMetrics17tightBoundingRectERK7QString",
    "_ZNK12QFontMetrics10elidedTextERK7QStringN2Qt13TextElideModeEii",
    # QFontMetricsF floating-point layout hooks.
    "_ZNK13QFontMetricsF17horizontalAdvanceERK7QStringi",
    "_ZNK13QFontMetricsF5widthERK7QString",
    "_ZNK13QFontMetricsF12boundingRectERK7QString",
    "_ZNK13QFontMetricsF12boundingRectERK6QRectFiRK7QStringiPi",
    "_ZNK13QFontMetricsF4sizeEiRK7QStringiPi",
    "_ZNK13QFontMetricsF17tightBoundingRectERK7QString",
    "_ZNK13QFontMetricsF10elidedTextERK7QStringN2Qt13TextElideModeEdi",
)


def exported_names(image: bytes) -> set[str]:
    """Read the PE export-name table without executing the DLL."""
    if len(image) < 0x40 or image[:2] != b"MZ":
        raise ValueError("not a DOS/PE image")
    pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
    if pe_offset + 24 > len(image) or image[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    section_count = struct.unpack_from("<H", image, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", image, pe_offset + 20)[0]
    optional = pe_offset + 24
    if optional + optional_size > len(image):
        raise ValueError("truncated optional header")
    magic = struct.unpack_from("<H", image, optional)[0]
    data_directory = optional + (112 if magic == 0x20B else 96 if magic == 0x10B else -1)
    if data_directory < optional or data_directory + 8 > optional + optional_size:
        raise ValueError("unsupported or truncated optional header")
    export_rva, export_size = struct.unpack_from("<II", image, data_directory)
    if not export_rva or not export_size:
        return set()

    sections = []
    section_table = optional + optional_size
    if section_count > 96 or section_table + section_count * 40 > len(image):
        raise ValueError("invalid PE section table")
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", image, offset + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset, raw_size))

    def file_offset(rva: int, size: int = 1) -> int:
        for virtual_address, span, raw_offset, raw_size in sections:
            relative = rva - virtual_address
            if 0 <= relative and relative + size <= span and relative + size <= raw_size:
                result = raw_offset + relative
                if result + size <= len(image):
                    return result
        raise ValueError(f"unmapped PE RVA 0x{rva:x}")

    directory = file_offset(export_rva, 40)
    name_count = struct.unpack_from("<I", image, directory + 24)[0]
    names_rva = struct.unpack_from("<I", image, directory + 32)[0]
    if name_count > 1_000_000:
        raise ValueError("unreasonable PE export count")
    names_offset = file_offset(names_rva, name_count * 4)
    result = set()
    for index in range(name_count):
        name_rva = struct.unpack_from("<I", image, names_offset + index * 4)[0]
        start = file_offset(name_rva)
        end = image.find(b"\0", start, min(len(image), start + 8192))
        if end < 0:
            raise ValueError("unterminated PE export name")
        try:
            result.add(image[start:end].decode("ascii"))
        except UnicodeDecodeError as error:
            raise ValueError("non-ASCII PE export name") from error
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt5gui", type=Path)
    args = parser.parse_args()
    if not args.qt5gui.is_file():
        parser.error(f"Qt5Gui.dll not found: {args.qt5gui}")

    try:
        names = exported_names(args.qt5gui.read_bytes())
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    missing = [symbol for symbol in SYMBOLS if symbol not in names]
    if missing:
        for symbol in missing:
            print(f"missing={symbol}")
        return 1
    print(f"qt_hook_exports={len(SYMBOLS)} missing=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
