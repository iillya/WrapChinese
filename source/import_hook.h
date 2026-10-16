#pragma once
#include <windows.h>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <vector>

// Shared by drawing and UI metric hooks; preserves the MinGW function ABI.
namespace WrapImportHooks {
struct PatchRecord { HMODULE module; void** slot; void* original; void* replacement; };
inline bool executableAddress(const void* address) {
    MEMORY_BASIC_INFORMATION memory{};
    return address && VirtualQuery(address, &memory, sizeof(memory)) &&
        memory.State == MEM_COMMIT &&
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
        !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

template <typename Function>
Function resolveFunction(HMODULE module, const char* symbol) {
    const FARPROC address = module ? GetProcAddress(module, symbol) : nullptr;
    if (!executableAddress(reinterpret_cast<const void*>(address))) return nullptr;
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

struct Replacement { void* original; void* replacement; };

inline bool readable(const void* address, std::size_t bytes, HMODULE module) {
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    if (cursor + bytes < cursor) return false;
    const auto end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || memory.AllocationBase != module ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}

inline bool replaceImports(HMODULE module, const Replacement* symbols, std::size_t count,
                           std::vector<PatchRecord>* records = nullptr) {
    if (!module || !symbols || !count) return false;
    // Keep an unloading DLL alive while inspecting and patching its image.
    HMODULE retained = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(module), &retained)) return false;
    struct Release { HMODULE module; ~Release() { FreeLibrary(module); } } release{retained};
    auto* base = reinterpret_cast<unsigned char*>(module);
    if (!readable(base, sizeof(IMAGE_DOS_HEADER), module)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < LONG(sizeof(*dos)) ||
        dos->e_lfanew > 1024 * 1024 ||
        !readable(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS64), module)) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) return false;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
    if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        directory.VirtualAddress >= imageSize || directory.Size > imageSize - directory.VirtualAddress ||
        !readable(base + directory.VirtualAddress, directory.Size, module)) return false;
    bool changed = false;
    const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    for (std::size_t i = 0; i < directory.Size / sizeof(*descriptors) && descriptors[i].Name; ++i) {
        const auto rva = descriptors[i].FirstThunk;
        if (!rva || rva >= imageSize) continue;
        auto* thunks = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + rva);
        std::uintptr_t readableEnd = 0;
        for (std::size_t j = 0; j < (imageSize - rva) / sizeof(*thunks); ++j) {
            auto** slot = reinterpret_cast<void**>(&thunks[j].u1.Function);
            const auto address = reinterpret_cast<std::uintptr_t>(slot);
            // IAT entries share pages; validate each memory region once rather
            // than issuing a kernel query for every imported function.
            if (address + sizeof(void*) > readableEnd) {
                MEMORY_BASIC_INFORMATION memory{};
                if (!VirtualQuery(slot, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
                    memory.AllocationBase != module || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) break;
                readableEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
                if (address + sizeof(void*) > readableEnd) break;
            }
            if (!*slot) break;
            for (std::size_t k = 0; k < count; ++k) {
                const auto& symbol = symbols[k];
                if (!symbol.original || !symbol.replacement || *slot != symbol.original) continue;
                if (records) records->push_back({module, slot, symbol.original, symbol.replacement});
                DWORD oldProtect = 0;
                if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                    if (records) records->pop_back();
                    break;
                }
                const bool replaced = InterlockedCompareExchangePointer(
                    slot, symbol.replacement, symbol.original) == symbol.original;
                DWORD ignored = 0;
                VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
                if (replaced) changed = true;
                else if (records) records->pop_back();
                break;
            }
        }
    }
    return changed;
}

inline bool replaceImports(HMODULE module, void* original, void* replacement,
                           std::vector<PatchRecord>* records = nullptr) {
    const Replacement symbol{original, replacement};
    return replaceImports(module, &symbol, 1, records);
}

inline void restoreImports(std::vector<PatchRecord>& records) {
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(it->slot, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || memory.AllocationBase != it->module ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) continue;
        DWORD oldProtect = 0;
        if (!VirtualProtect(it->slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) continue;
        InterlockedCompareExchangePointer(it->slot, it->original, it->replacement);
        DWORD ignored = 0;
        VirtualProtect(it->slot, sizeof(void*), oldProtect, &ignored);
    }
    records.clear();
}

} // namespace WrapImportHooks
