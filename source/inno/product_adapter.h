// Product adaptation for the shared Inno support DLL. Included inside support.cpp.
#include "product_config.h"

bool productQt(const std::wstring& root) {
    if (kQtMajors.empty()) return true;
    unsigned major = 0;
    for (const DWORD candidate : kQtMajors) {
        const auto core = root + L"\\Qt" + std::to_wstring(candidate) + L"Core.dll";
        if (regularFile(core)) {
            if (major && major != candidate) return false;
            major = candidate;
        }
    }
    if (!major) return false;
    std::vector<DWORD> versionSeen;
    for (const auto* module : kQtModules) {
        const auto file = root + L"\\Qt" + std::to_wstring(major) + module + L".dll";
        if (!regularFile(file) || !amd64(file)) return false;
        DWORD ignored = 0;
        const DWORD length = GetFileVersionInfoSizeW(file.c_str(), &ignored);
        if (!length || length > 1024 * 1024) return false;
        std::vector<BYTE> bytes(length);
        if (!GetFileVersionInfoW(file.c_str(), 0, length, bytes.data())) return false;
        VS_FIXEDFILEINFO* info = nullptr; UINT size = 0;
        if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &size) ||
            size < sizeof(*info) || info->dwSignature != 0xfeef04bd) return false;
        const std::vector<DWORD> current{
            HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
            HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
        if (current[0] != major ||
            (!versionSeen.empty() && versionSeen != current)) return false;
        versionSeen = current;
    }
    return true;
}
