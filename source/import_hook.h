#pragma once
#include <windows.h>
#include <cstring>

// Shared by drawing and UI metric hooks; preserves the MinGW function ABI.
namespace WrapImportHooks {
template <typename Function>
Function resolveFunction(HMODULE module, const char* symbol) {
    const FARPROC address = GetProcAddress(module, symbol);
    static_assert(sizeof(Function) == sizeof(address),
                  "Function pointer size does not match FARPROC");
    Function function = nullptr;
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

template <typename Function>
void* functionAddress(Function function) {
    static_assert(sizeof(Function) == sizeof(void*),
                  "Function pointer size does not match data pointer");
    void* address = nullptr;
    std::memcpy(&address, &function, sizeof(address));
    return address;
}

bool replaceImports(HMODULE module, void* original, void* replacement) {
    if (!module || !original || !replacement) return false;
    auto* base = reinterpret_cast<unsigned char*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const DWORD importRva = nt->OptionalHeader
        .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva) return false;
    bool changed = false;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->FirstThunk) continue;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; thunk->u1.Function; ++thunk) {
            auto** slot = reinterpret_cast<void**>(&thunk->u1.Function);
            if (*slot != original) continue;
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) continue;
            const bool replaced = InterlockedCompareExchangePointer(
                slot, replacement, original) == original;
            DWORD ignored = 0;
            VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
            if (replaced) changed = true;
        }
    }
    return changed;
}

} // namespace WrapImportHooks
