#pragma once
#include <string>
#include <vector>

inline constexpr wchar_t kHostName[] = L"Wrap.exe";
inline constexpr wchar_t kLauncher[] = L"WrapChineseLauncher.exe";
inline const std::vector<const wchar_t*> kKnownHandlers = {
    L"WrapProjectFile"
};
inline const std::vector<const wchar_t*> kExtensions = {L".wrap"};
inline const std::vector<const wchar_t*> kQtModules = {
    L"Core", L"Gui", L"Widgets"
};
inline const std::vector<std::vector<DWORD>> kQtVersions = {{5, 15, 3}};
