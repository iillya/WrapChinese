#include "../source/launcher.cpp"
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && wcscmp(argv[1], L"--child") == 0) {
        Sleep(static_cast<DWORD>(_wtoi(argv[2])));
        return 37;
    }
    const auto actual = remoteLoadLibrary(GetCurrentProcessId());
    const auto expected = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!actual || reinterpret_cast<uintptr_t>(actual) != reinterpret_cast<uintptr_t>(expected))
        return 1;
    std::puts("PASS: launcher resolves LoadLibraryW from its owning module");

    const std::wstring executable = executablePath();
    if (!waitForExistingWrapTransition(executable, executable, 0)) return 2;
    std::wstring command = L"\"" + executable + L"\" --child 2000";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &child)) return 3;
    Sleep(250);
    const bool timedOut = !waitForExistingWrapTransition(executable, executable, 200);
    const bool waited = waitForExistingWrapTransition(executable, executable, 5000);
    DWORD exitCode = 0;
    const bool codeMatches = GetExitCodeProcess(child.hProcess, &exitCode) && exitCode == 37;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    if (!timedOut || !waited || !codeMatches) return 4;
    std::puts("PASS: launcher waits for closing translated host and honors timeout");
    return 0;
}
