#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>

#include "../source/inno/association_proxy.h"

#include <iostream>
#include <string>

namespace {

bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool rejectBeforeApply(const wchar_t* phase, unsigned) {
    return wcscmp(phase, L"before-apply") != 0;
}

} // namespace

int main() {
    const std::wstring testPath =
        L"Software\\WrapChineseLocalizer\\Tests\\" +
        std::to_wstring(GetCurrentProcessId());
    SHDeleteKeyW(HKEY_CURRENT_USER, testPath.c_str());

    HKEY base = nullptr;
    HKEY user = nullptr;
    HKEY machine = nullptr;
    bool ok = require(RegCreateKeyExW(
        HKEY_CURRENT_USER, testPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
        nullptr, &base, nullptr) == ERROR_SUCCESS, "test registry root created");
    if (ok) {
        ok &= require(RegCreateKeyExW(base, L"User", 0, nullptr, 0,
                                     KEY_ALL_ACCESS, nullptr, &user,
                                     nullptr) == ERROR_SUCCESS,
                      "isolated user hive created");
        ok &= require(RegCreateKeyExW(base, L"Machine", 0, nullptr, 0,
                                     KEY_ALL_ACCESS, nullptr, &machine,
                                     nullptr) == ERROR_SUCCESS,
                      "isolated machine hive created");
    }

    const std::wstring root = L"C:\\Wrap Test";
    const std::wstring official = root + L"\\Wrap.exe";
    const std::wstring launcher =
        root + L"\\ChineseLauncher\\WrapChineseLauncher.exe";
    const std::wstring expectedProxy =
        L"\"" + launcher + L"\" \"%1\" --reuse";
    const auto machineCommand = WrapProxy::stringValue(
        L"\"" + official + L"\" \"%1\" --reuse");
    if (ok) {
        ok &= require(WrapProxy::write(
            machine,
            L"Software\\Classes\\WrapProjectFile\\shell\\open\\command",
            nullptr, machineCommand), "official machine command seeded");
    }

    std::wstring error;
    if (ok) {
        ok &= require(WrapProxy::install(user, machine, root, error),
                      "proxy installed");
        WrapProxy::Value current;
        std::wstring command;
        ok &= require(WrapProxy::read(
                          user,
                          L"Software\\Classes\\WrapProjectFile\\shell\\open\\command",
                          nullptr, current) &&
                          WrapProxy::text(current, command) &&
                          command == expectedProxy,
                      "arguments preserved in proxy command");
        ok &= require(WrapProxy::install(user, machine, root, error),
                      "proxy reinstall is idempotent");
        ok &= require(WrapProxy::uninstall(user, root, error),
                      "proxy uninstalled");
        current = {};
        ok &= require(WrapProxy::read(
                          user,
                          L"Software\\Classes\\WrapProjectFile\\shell\\open\\command",
                          nullptr, current) && !current.present,
                      "missing original command restored as missing");
    }

    if (ok) {
        error.clear();
        ok &= require(!WrapProxy::install(
                          user, machine, root, error, rejectBeforeApply),
                      "faulted apply rejected");
        WrapProxy::Value current;
        ok &= require(WrapProxy::read(
                          user,
                          L"Software\\Classes\\WrapProjectFile\\shell\\open\\command",
                          nullptr, current) && !current.present,
                      "faulted apply rolled command back");
        WrapProxy::Value owner;
        ok &= require(WrapProxy::read(
                          user, L"Software\\WrapChineseLocalizer\\InnoProxyV1\\0",
                          L"Owner", owner) && !owner.present,
                      "faulted apply cleared new journal");
    }

    if (ok) {
        error.clear();
        ok &= require(WrapProxy::install(user, machine, root, error),
                      "proxy reinstalled for ownership test");
        const auto external = WrapProxy::stringValue(
            L"\"C:\\Other\\Other.exe\" \"%1\"");
        ok &= require(WrapProxy::write(
                          user,
                          L"Software\\Classes\\WrapProjectFile\\shell\\open\\command",
                          nullptr, external), "external change simulated");
        ok &= require(WrapProxy::uninstall(user, root, error),
                      "uninstall accepted external replacement");
        WrapProxy::Value current;
        ok &= require(WrapProxy::read(
                          user,
                          L"Software\\Classes\\WrapProjectFile\\shell\\open\\command",
                          nullptr, current) && current == external,
                      "external replacement preserved");
    }

    if (machine) RegCloseKey(machine);
    if (user) RegCloseKey(user);
    if (base) RegCloseKey(base);
    const LSTATUS cleanup = SHDeleteKeyW(HKEY_CURRENT_USER, testPath.c_str());
    ok &= require(cleanup == ERROR_SUCCESS || cleanup == ERROR_FILE_NOT_FOUND,
                  "isolated registry test tree removed");

    std::cout << "association_proxy_tests=" << ok << '\n';
    return ok ? 0 : 1;
}
