#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdint>
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
    return haveVersion;
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
