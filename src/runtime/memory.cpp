#include "runtime/memory.h"

#include <windows.h>

#include <cstring>
#include <limits>

namespace fair_transmog::mem {
namespace {

bool HasProtection(DWORD protection, bool require_write) noexcept {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const DWORD access = protection & 0xffU;
    if (require_write) {
        return access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
               access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
    }
    return access == PAGE_READONLY || access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
           access == PAGE_EXECUTE_READ || access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

bool HasAccess(const void* address, size_t size, bool require_write) noexcept {
    if (size == 0) {
        return true;
    }
    if (address == nullptr) {
        return false;
    }

    const auto begin = reinterpret_cast<uintptr_t>(address);
    if (size > std::numeric_limits<uintptr_t>::max() - begin) {
        return false;
    }
    const uintptr_t end = begin + size;
    uintptr_t current = begin;

    while (current < end) {
        MEMORY_BASIC_INFORMATION information{};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &information, sizeof(information)) !=
            sizeof(information)) {
            return false;
        }
        if (information.State != MEM_COMMIT || !HasProtection(information.Protect, require_write)) {
            return false;
        }

        const auto region_begin = reinterpret_cast<uintptr_t>(information.BaseAddress);
        if (information.RegionSize > std::numeric_limits<uintptr_t>::max() - region_begin) {
            return false;
        }
        const uintptr_t region_end = region_begin + information.RegionSize;
        if (region_end <= current) {
            return false;
        }
        current = region_end < end ? region_end : end;
    }
    return true;
}

}  // namespace

bool Read(const void* source, void* destination, size_t size) noexcept {
    if (size == 0) {
        return true;
    }
    if (!HasAccess(source, size, false) || !HasAccess(destination, size, true)) {
        return false;
    }

    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Write(void* destination, const void* source, size_t size) noexcept {
    if (size == 0) {
        return true;
    }
    if (!HasAccess(destination, size, true) || !HasAccess(source, size, false)) {
        return false;
    }

    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Matches(const void* address, std::span<const uint8_t> expected) noexcept {
    if (expected.empty()) {
        return true;
    }
    if (!HasAccess(address, expected.size(), false)) {
        return false;
    }

    __try {
        return std::memcmp(address, expected.data(), expected.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

PointerPatchResult ReplacePointer(void** slot, void* expected, void* replacement) noexcept {
    if (reinterpret_cast<uintptr_t>(slot) % alignof(void*) != 0 ||
        !HasAccess(slot, sizeof(*slot), false)) {
        return {false, false};
    }
    MEMORY_BASIC_INFORMATION information{};
    if (VirtualQuery(slot, &information, sizeof(information)) != sizeof(information) ||
        (information.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                PAGE_EXECUTE_WRITECOPY)) != 0) {
        return {false, false};
    }

    DWORD original_protection = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &original_protection)) {
        return {false, false};
    }

    void* const previous = InterlockedCompareExchangePointer(slot, replacement, expected);
    DWORD ignored = 0;
    const bool protection_restored =
        VirtualProtect(slot, sizeof(*slot), original_protection, &ignored) != FALSE;
    return {previous == expected, protection_restored};
}

}  // namespace fair_transmog::mem
