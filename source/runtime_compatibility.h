#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace WrapChineseCompatibility {

inline constexpr std::uint64_t kSupportedHostSize = 35733624;
inline constexpr std::array<unsigned char, 32> kSupportedHostSha256 = {
    0x1a, 0x28, 0x8e, 0x06, 0xec, 0x62, 0xe7, 0x6d,
    0x11, 0x10, 0xcd, 0xbc, 0x35, 0xa5, 0x59, 0x56,
    0x96, 0xa4, 0x6f, 0xa8, 0x3a, 0xb9, 0x93, 0xd1,
    0x3b, 0xd3, 0xa3, 0x0c, 0xe3, 0x36, 0x6b, 0xc9,
};

class FileHandle final {
public:
    explicit FileHandle(HANDLE value) : value_(value) {}
    ~FileHandle() {
        if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

class AlgorithmHandle final {
public:
    ~AlgorithmHandle() {
        if (value) BCryptCloseAlgorithmProvider(value, 0);
    }
    BCRYPT_ALG_HANDLE value = nullptr;
};

class HashHandle final {
public:
    ~HashHandle() {
        if (value) BCryptDestroyHash(value);
    }
    BCRYPT_HASH_HANDLE value = nullptr;
};

inline bool matchesSupportedHost(const std::wstring& path) {
    FileHandle file(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info) ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    const std::uint64_t size =
        (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32U) |
        info.nFileSizeLow;
    if (size != kSupportedHostSize) return false;

    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0) return false;
    HashHandle hash;
    if (BCryptCreateHash(algorithm.value, &hash.value, nullptr, 0, nullptr, 0,
                         0) < 0) return false;

    std::vector<unsigned char> buffer(1024 * 1024);
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(file.get(), buffer.data(),
                      static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            return false;
        }
        if (!bytesRead) break;
        if (BCryptHashData(hash.value, buffer.data(), bytesRead, 0) < 0)
            return false;
    }
    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash.value, digest.data(),
                         static_cast<ULONG>(digest.size()), 0) < 0) return false;
    return digest == kSupportedHostSha256;
}

} // namespace WrapChineseCompatibility
