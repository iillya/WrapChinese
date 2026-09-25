// Wrap Chinese launcher: starts Wrap suspended, injects the localizer DLL,
// then resumes the unmodified signed host executable.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "runtime_compatibility.h"

#include <string>
#include <vector>

namespace {

std::wstring parentDirectory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool isFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

std::wstring executablePath() {
    std::vector<wchar_t> buffer(1024);
    while (buffer.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size() - 1)
            return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

bool validRoot(const std::wstring& root) {
    return WrapChineseCompatibility::supportsHostDirectory(root);
}

std::wstring findCompatibleChildRoot(const std::wstring& parent) {
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW((parent + L"\\Wrap *").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return {};
    std::wstring selected;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        const std::wstring candidate = parent + L"\\" + data.cFileName;
        if (validRoot(candidate) &&
            (selected.empty() || candidate > selected)) {
            selected = candidate;
        }
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return selected;
}

std::wstring findWrapRoot(const std::wstring& start) {
    std::wstring directory = start;
    for (int depth = 0; depth < 12 && !directory.empty(); ++depth) {
        if (validRoot(directory)) return directory;
        const std::wstring parent = parentDirectory(directory);
        if (parent.empty() || parent == directory) break;
        directory = parent;
    }

    wchar_t programFiles[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(
        L"ProgramFiles", programFiles, MAX_PATH);
    if (!length || length >= MAX_PATH)
        length = GetEnvironmentVariableW(
            L"ProgramW6432", programFiles, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};

    return findCompatibleChildRoot(std::wstring(programFiles) + L"\\Faceform");
}

HANDLE moduleSnapshot(DWORD processId) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
        if (snapshot != INVALID_HANDLE_VALUE) return snapshot;
        if (GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(1);
    }
    return INVALID_HANDLE_VALUE;
}

LPTHREAD_START_ROUTINE remoteLoadLibrary(DWORD processId) {
    HMODULE localKernel = GetModuleHandleW(L"kernel32.dll");
    FARPROC localFunction = localKernel
        ? GetProcAddress(localKernel, "LoadLibraryW") : nullptr;
    if (!localFunction) return nullptr;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(localFunction) -
                             reinterpret_cast<uintptr_t>(localKernel);
    HANDLE snapshot = moduleSnapshot(processId);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, L"kernel32.dll") == 0) {
                    CloseHandle(snapshot);
                    return reinterpret_cast<LPTHREAD_START_ROUTINE>(
                        reinterpret_cast<uintptr_t>(entry.modBaseAddr) + offset);
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    // A CREATE_SUSPENDED process can expose an incomplete loader list even
    // though the system DLL mappings already exist. Windows shares the same
    // ASLR mapping for kernel32 within a boot session; the post-load module
    // check below still fails closed if that assumption does not hold.
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(localFunction);
}

HMODULE findInjectedModule(DWORD processId, const std::wstring& expectedPath) {
    HANDLE snapshot = moduleSnapshot(processId);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
    HMODULE found = nullptr;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExePath, expectedPath.c_str()) == 0) {
                found = entry.hModule;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

LPTHREAD_START_ROUTINE remoteExport(const std::wstring& modulePath,
                                    HMODULE remoteModule,
                                    const char* exportName) {
    if (!remoteModule || !exportName) return nullptr;
    HMODULE localModule = LoadLibraryExW(
        modulePath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!localModule) return nullptr;
    FARPROC localExport = GetProcAddress(localModule, exportName);
    LPTHREAD_START_ROUTINE result = nullptr;
    if (localExport) {
        const uintptr_t offset = reinterpret_cast<uintptr_t>(localExport) -
                                 reinterpret_cast<uintptr_t>(localModule);
        result = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<uintptr_t>(remoteModule) + offset);
    }
    FreeLibrary(localModule);
    return result;
}

void showError(const wchar_t* message) {
    wchar_t quiet[4] = {};
    if (GetEnvironmentVariableW(L"WRAP_CHINESE_QUIET_TEST", quiet, 4) &&
        quiet[0] == L'1') {
        OutputDebugStringW(message);
        return;
    }
    MessageBoxW(nullptr, message, L"Wrap 中文补丁", MB_OK | MB_ICONERROR);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    const std::wstring ownPath = executablePath();
    const std::wstring launcherDirectory = parentDirectory(ownPath);
    const std::wstring wrapRoot = findWrapRoot(launcherDirectory);
    const std::wstring hookPath = launcherDirectory + L"\\WrapChineseHook.dll";
    const std::wstring hostPath = wrapRoot + L"\\Wrap.exe";

    if (wrapRoot.empty() || !isFile(hostPath) || !isFile(hookPath)) {
        showError(L"找不到 Wrap.exe 或 WrapChineseHook.dll。\n"
                  L"请将完整补丁安装到 Wrap 的 ChineseLauncher 目录。");
        return 1;
    }

    std::wstring fullCommand = L"\"" + hostPath + L"\"";
    if (commandLine && *commandLine) {
        fullCommand += L" ";
        fullCommand += commandLine;
    }
    std::vector<wchar_t> mutableCommand(fullCommand.begin(), fullCommand.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    wchar_t headless[4] = {};
    if (GetEnvironmentVariableW(L"WRAP_CHINESE_HEADLESS_CAPTURE", headless, 4) &&
        headless[0] == L'1') {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(hostPath.c_str(), mutableCommand.data(), nullptr, nullptr,
                        FALSE, CREATE_SUSPENDED, nullptr, wrapRoot.c_str(),
                        &startup, &process)) {
        showError(L"无法启动 Wrap.exe。");
        return 2;
    }

    wchar_t readyEventName[96] = {};
    swprintf_s(readyEventName, L"Local\\WrapChineseHookReady_%lu",
               process.dwProcessId);
    HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, readyEventName);

    const size_t pathBytes = (hookPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(
        process.hProcess, nullptr, pathBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T bytesWritten = 0;
    const bool wrotePath = remotePath &&
        WriteProcessMemory(process.hProcess, remotePath, hookPath.c_str(),
                           pathBytes, &bytesWritten) &&
        bytesWritten == pathBytes;
    LPTHREAD_START_ROUTINE loader = remoteLoadLibrary(process.dwProcessId);
    HANDLE injectionThread = wrotePath && loader
        ? CreateRemoteThread(process.hProcess, nullptr, 0, loader,
                             remotePath, 0, nullptr)
        : nullptr;

    HMODULE remoteHook = nullptr;
    bool completed = false;
    if (injectionThread) {
        const DWORD waitResult = WaitForSingleObject(injectionThread, 30000);
        completed = waitResult == WAIT_OBJECT_0;
        if (completed)
            remoteHook = findInjectedModule(process.dwProcessId, hookPath);
        CloseHandle(injectionThread);
    }

    bool initialized = false;
    LPTHREAD_START_ROUTINE initializer = remoteExport(
        hookPath, remoteHook, "WrapChineseInitialize");
    HANDLE initializationThread = initializer
        ? CreateRemoteThread(process.hProcess, nullptr, 0, initializer,
                             nullptr, 0, nullptr)
        : nullptr;
    if (initializationThread) {
        const DWORD waitResult = WaitForSingleObject(initializationThread, 30000);
        DWORD result = 0;
        initialized = waitResult == WAIT_OBJECT_0 &&
                      GetExitCodeThread(initializationThread, &result) &&
                      result != 0;
        CloseHandle(initializationThread);
    }

    const bool resumed = initialized && readyEvent &&
                         ResumeThread(process.hThread) != DWORD(-1);
    const bool hookReady = resumed &&
        WaitForSingleObject(readyEvent, 60000) == WAIT_OBJECT_0;
    if (hookReady) {
        if (remotePath && completed)
            VirtualFreeEx(process.hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(readyEvent);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 0;
    }

    TerminateProcess(process.hProcess, 3);
    WaitForSingleObject(process.hProcess, 5000);
    if (remotePath && completed)
        VirtualFreeEx(process.hProcess, remotePath, 0, MEM_RELEASE);
    if (readyEvent) CloseHandle(readyEvent);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    showError(L"当前 Wrap 的 Qt5 能力不足，或汉化 DLL 初始化失败。\n"
              L"原版程序文件没有被修改。");
    return 3;
}
