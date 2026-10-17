#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace fair_transmog::mem {

bool Read(const void* source, void* destination, size_t size) noexcept;

template <typename T>
std::optional<T> Read(const void* source) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    if (!Read(source, &value, sizeof(value))) {
        return std::nullopt;
    }
    return value;
}

bool Write(void* destination, const void* source, size_t size) noexcept;
bool Matches(const void* address, std::span<const uint8_t> expected) noexcept;

struct PointerPatchResult {
    bool installed;
    bool protection_restored;
};

// Only naturally aligned slots on non-executable pages may be replaced.
PointerPatchResult ReplacePointer(void** slot, void* expected, void* replacement) noexcept;

}  // namespace fair_transmog::mem
