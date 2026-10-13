#pragma once
#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

namespace InstallerUser {
// Resolve the logged-on session account, never the alternate administrator
// account used to elevate Setup. This works without an explorer.exe process.
inline std::wstring sidForAccount(const std::wstring& account) {
    if (account.empty()) return {};
    DWORD sidBytes = 0, domainChars = 0;
    SID_NAME_USE kind{};
    LookupAccountNameW(nullptr, account.c_str(), nullptr, &sidBytes, nullptr, &domainChars, &kind);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !sidBytes || sidBytes > 65536) return {};
    std::vector<BYTE> sid(sidBytes);
    std::vector<wchar_t> domain(domainChars + 1);
    if (!LookupAccountNameW(nullptr, account.c_str(), sid.data(), &sidBytes,
            domain.data(), &domainChars, &kind) || kind != SidTypeUser || !IsValidSid(sid.data())) return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid.data(), &text)) return {};
    std::wstring result(text);
    LocalFree(text);
    return result;
}
inline std::wstring sessionSid(DWORD session) {
    HMODULE library = LoadLibraryExW(L"wtsapi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return {};
    using Query = BOOL (WINAPI*)(HANDLE, DWORD, WTS_INFO_CLASS, LPWSTR*, DWORD*);
    using Free = void (WINAPI*)(PVOID);
    const auto query = reinterpret_cast<Query>(GetProcAddress(library, "WTSQuerySessionInformationW"));
    const auto release = reinterpret_cast<Free>(GetProcAddress(library, "WTSFreeMemory"));
    std::wstring user, domain;
    if (query && release) {
        auto read = [&](WTS_INFO_CLASS field) {
            LPWSTR value = nullptr;
            DWORD bytes = 0;
            std::wstring result;
            if (query(nullptr, session, field, &value, &bytes) && value && bytes >= sizeof(wchar_t))
                result.assign(value);
            if (value) release(value);
            return result;
        };
        user = read(WTSUserName);
        domain = read(WTSDomainName);
    }
    FreeLibrary(library);
    return user.empty() ? std::wstring{} : sidForAccount(domain.empty() ? user : domain + L"\\" + user);
}
inline std::wstring interactiveSid() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session) || !session) return {};
    const auto accountSid = sessionSid(session);
    if (!accountSid.empty()) return accountSid;
    // Compatibility fallback for systems where session-account lookup fails.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W item{};
    item.dwSize = sizeof(item);
    std::wstring result;
    if (Process32FirstW(snapshot, &item)) do {
        DWORD candidateSession = 0;
        if (_wcsicmp(item.szExeFile, L"explorer.exe") ||
            !ProcessIdToSessionId(item.th32ProcessID, &candidateSession) || candidateSession != session) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.th32ProcessID);
        HANDLE token = nullptr;
        if (process && OpenProcessToken(process, TOKEN_QUERY, &token)) {
            DWORD bytes = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
            if (bytes && bytes <= 65536) {
                std::vector<BYTE> data(bytes);
                LPWSTR sid = nullptr;
                if (GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes) &&
                    ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) {
                    result = sid;
                    LocalFree(sid);
                }
            }
        }
        if (token) CloseHandle(token);
        if (process) CloseHandle(process);
        if (!result.empty()) break;
    } while (Process32NextW(snapshot, &item));
    CloseHandle(snapshot);
    return result;
}
}
