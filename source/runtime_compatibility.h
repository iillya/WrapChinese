#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace WrapChineseCompatibility {

struct FileVersion final {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
    std::uint16_t build = 0;

    bool operator==(const FileVersion& other) const noexcept {
        return major == other.major && minor == other.minor &&
               patch == other.patch && build == other.build;
    }
};

inline bool isRegularFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & (FILE_ATTRIBUTE_DIRECTORY |
                           FILE_ATTRIBUTE_REPARSE_POINT));
}

inline bool isAmd64Image(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    bool valid = ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
                 read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                 dos.e_lfanew >= static_cast<LONG>(sizeof(dos));
    LARGE_INTEGER offset{};
    offset.QuadPart = valid ? dos.e_lfanew : 0;
    valid = valid && SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);

    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    if (valid) {
        valid = ReadFile(file, &signature, sizeof(signature), &read, nullptr) &&
                read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
                ReadFile(file, &header, sizeof(header), &read, nullptr) &&
                read == sizeof(header) &&
                header.Machine == IMAGE_FILE_MACHINE_AMD64;
    }
    CloseHandle(file);
    return valid;
}

inline bool readFileVersion(const std::wstring& path, FileVersion& result) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 1024 * 1024) return false;
    std::vector<unsigned char> bytes(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return false;

    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info),
                        &length) ||
        length < sizeof(*info) || info->dwSignature != 0xfeef04bd) {
        return false;
    }
    result = {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
              HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
    return true;
}

// Inspect the on-disk export table without loading/executing another Qt ABI.
inline bool hasExport(const std::wstring& path, const char* expected) {
    struct Image {
        HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
        const unsigned char* bytes = nullptr;
        ~Image() {
            if (bytes) UnmapViewOfFile(bytes);
            if (mapping) CloseHandle(mapping);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        }
    } image;
    image.file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    LARGE_INTEGER length{};
    if (image.file == INVALID_HANDLE_VALUE || !GetFileSizeEx(image.file, &length) ||
        length.QuadPart < 64 || length.QuadPart > 128 * 1024 * 1024) return false;
    image.mapping = CreateFileMappingW(image.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!image.mapping) return false;
    image.bytes = static_cast<const unsigned char*>(MapViewOfFile(image.mapping, FILE_MAP_READ, 0, 0, 0));
    if (!image.bytes) return false;
    const auto size = static_cast<std::size_t>(length.QuadPart);
    const auto span = [&](std::size_t offset, std::size_t bytes) {
        return offset <= size && bytes <= size - offset;
    };
    IMAGE_DOS_HEADER dos{};
    if (!span(0, sizeof(dos))) return false;
    std::memcpy(&dos, image.bytes, sizeof(dos));
    IMAGE_NT_HEADERS64 nt{};
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < LONG(sizeof(dos)) ||
        !span(std::size_t(dos.e_lfanew), sizeof(nt))) return false;
    std::memcpy(&nt, image.bytes + dos.e_lfanew, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.FileHeader.NumberOfSections > 96 ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) return false;
    const std::size_t sectionOffset = std::size_t(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
    if (!span(sectionOffset, std::size_t(nt.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER))) return false;
    const auto atRva = [&](DWORD rva, std::size_t bytes) -> const unsigned char* {
        if (rva < nt.OptionalHeader.SizeOfHeaders && span(rva, bytes)) return image.bytes + rva;
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            std::memcpy(&section, image.bytes + sectionOffset + i * sizeof(section), sizeof(section));
            if (rva < section.VirtualAddress) continue;
            const std::size_t delta = rva - section.VirtualAddress;
            if (delta <= section.SizeOfRawData && bytes <= section.SizeOfRawData - delta &&
                span(std::size_t(section.PointerToRawData) + delta, bytes))
                return image.bytes + section.PointerToRawData + delta;
        }
        return nullptr;
    };
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const auto* exportBytes = atRva(directory.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY));
    if (!directory.VirtualAddress || !exportBytes) return false;
    IMAGE_EXPORT_DIRECTORY exports{};
    std::memcpy(&exports, exportBytes, sizeof(exports));
    if (!exports.NumberOfNames || exports.NumberOfNames > 65536) return false;
    const auto* names = atRva(exports.AddressOfNames, std::size_t(exports.NumberOfNames) * sizeof(DWORD));
    if (!names) return false;
    const auto expectedLength = std::strlen(expected) + 1;
    for (DWORD i = 0; i < exports.NumberOfNames; ++i) {
        DWORD rva = 0; std::memcpy(&rva, names + i * sizeof(DWORD), sizeof(rva));
        const auto* name = atRva(rva, expectedLength);
        if (name && std::memcmp(name, expected, expectedLength) == 0) return true;
    }
    return false;
}

inline bool supportsQt5Directory(const std::wstring& root) {
    constexpr std::array<const wchar_t*, 3> modules = {
        L"Qt5Core.dll", L"Qt5Gui.dll", L"Qt5Widgets.dll"};
    FileVersion runtime{};
    bool haveVersion = false;
    for (const wchar_t* module : modules) {
        const std::wstring path = root + L"\\" + module;
        FileVersion current{};
        if (!isRegularFile(path) || !isAmd64Image(path) ||
            !readFileVersion(path, current) || current.major != 5) {
            return false;
        }
        if (haveVersion && !(current == runtime)) return false;
        runtime = current;
        haveVersion = true;
    }
    return haveVersion &&
        hasExport(root + L"\\Qt5Core.dll", "_ZN7QObject16staticMetaObjectE") &&
        hasExport(root + L"\\Qt5Gui.dll", "_ZN8QPainter8drawTextERK7QPointFRK7QString") &&
        hasExport(root + L"\\Qt5Widgets.dll", "_ZN12QApplicationC1ERiPPci");
}

inline bool supportsHostDirectory(const std::wstring& root) {
    const std::wstring host = root + L"\\Wrap.exe";
    return isRegularFile(host) && isAmd64Image(host) &&
           supportsQt5Directory(root);
}

inline bool supportsRuntimeVersion(const char* version) noexcept {
    return version && version[0] == '5' && version[1] == '.';
}

} // namespace WrapChineseCompatibility
