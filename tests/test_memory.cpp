#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

#include "runtime/memory.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void TestRejectedAccess() {
    uint32_t value = 0x12345678;
    CHECK(!fair_transmog::mem::Read(nullptr, &value, sizeof(value)));
    CHECK(!fair_transmog::mem::Read(&value, nullptr, sizeof(value)));
    CHECK(!fair_transmog::mem::Write(nullptr, &value, sizeof(value)));
    CHECK(!fair_transmog::mem::Write(&value, nullptr, sizeof(value)));
    CHECK(fair_transmog::mem::Read(nullptr, nullptr, 0));
    CHECK(fair_transmog::mem::Write(nullptr, nullptr, 0));
    CHECK(fair_transmog::mem::Matches(nullptr, {}));

    auto* overflowing = reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max() - 1);
    CHECK(!fair_transmog::mem::Read(overflowing, &value, sizeof(value)));
    CHECK(!fair_transmog::mem::Write(overflowing, &value, sizeof(value)));
    CHECK(!fair_transmog::mem::Matches(nullptr, std::span<const uint8_t>(
                                                  reinterpret_cast<const uint8_t*>(&value), sizeof(value))));
    CHECK(value == 0x12345678);
    const auto invalid_patch = fair_transmog::mem::ReplacePointer(nullptr, nullptr, nullptr);
    CHECK(!invalid_patch.installed);
}

void TestReadWriteAndBounds() {
    SYSTEM_INFO system_information{};
    GetSystemInfo(&system_information);
    const size_t page_size = system_information.dwPageSize;
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, page_size * 2, MEM_RESERVE, PAGE_NOACCESS));
    CHECK(page != nullptr);
    if (page == nullptr) {
        return;
    }
    const bool committed = VirtualAlloc(page, page_size, MEM_COMMIT, PAGE_READWRITE) == page;
    CHECK(committed);
    if (!committed) {
        VirtualFree(page, 0, MEM_RELEASE);
        return;
    }

    const uint32_t input = 0x12345678;
    CHECK(fair_transmog::mem::Write(page, &input, sizeof(input)));
    const auto output = fair_transmog::mem::Read<uint32_t>(page);
    CHECK(output.has_value());
    CHECK(output.has_value() && *output == input);
    CHECK(fair_transmog::mem::Matches(page, std::span<const uint8_t>(
                                           reinterpret_cast<const uint8_t*>(&input), sizeof(input))));
    CHECK(!fair_transmog::mem::Read(page + page_size - 1, &page[0], 2));
    CHECK(!fair_transmog::mem::Matches(page + page_size - 1, std::span<const uint8_t>(page, 2)));

    DWORD old_protection = 0;
    CHECK(VirtualProtect(page, page_size, PAGE_READONLY, &old_protection) != FALSE);
    CHECK(!fair_transmog::mem::Write(page, &input, sizeof(input)));
    CHECK(fair_transmog::mem::Read<uint32_t>(page).has_value());
    CHECK(VirtualProtect(page, page_size, PAGE_NOACCESS, &old_protection) != FALSE);
    CHECK(!fair_transmog::mem::Read<uint32_t>(page).has_value());
    CHECK(VirtualProtect(page, page_size, PAGE_READWRITE | PAGE_GUARD, &old_protection) != FALSE);
    CHECK(!fair_transmog::mem::Read<uint32_t>(page).has_value());

    VirtualFree(page, 0, MEM_RELEASE);
}

void TestExecutablePointerRejected() {
    SYSTEM_INFO system_information{};
    GetSystemInfo(&system_information);
    auto* page = static_cast<void**>(VirtualAlloc(nullptr, system_information.dwPageSize,
                                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page != nullptr);
    if (page == nullptr) {
        return;
    }
    void* const first = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    void* const second = reinterpret_cast<void*>(static_cast<uintptr_t>(0x2222));
    *page = first;
    DWORD old_protection = 0;
    const bool protected_page = VirtualProtect(page, system_information.dwPageSize,
                                               PAGE_EXECUTE_READ, &old_protection) != FALSE;
    CHECK(protected_page);
    if (protected_page) {
        const auto rejected = fair_transmog::mem::ReplacePointer(page, first, second);
        CHECK(!rejected.installed);
        CHECK(*page == first);
        MEMORY_BASIC_INFORMATION information{};
        CHECK(VirtualQuery(page, &information, sizeof(information)) == sizeof(information));
        CHECK((information.Protect & 0xffU) == PAGE_EXECUTE_READ);
    }
    VirtualFree(page, 0, MEM_RELEASE);
}

void TestReplacePointer() {
    constexpr size_t page_size = 4096;
    auto* page = static_cast<void**>(VirtualAlloc(nullptr, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page != nullptr);
    if (page == nullptr) {
        return;
    }

    void* const first = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1111));
    void* const second = reinterpret_cast<void*>(static_cast<uintptr_t>(0x2222));
    void* const third = reinterpret_cast<void*>(static_cast<uintptr_t>(0x3333));
    *page = first;

    auto* unaligned = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(page) + 17);
    const auto rejected = fair_transmog::mem::ReplacePointer(unaligned, nullptr, second);
    CHECK(!rejected.installed);
    const auto untouched = fair_transmog::mem::Read<uintptr_t>(unaligned);
    CHECK(untouched.has_value() && *untouched == 0);

    DWORD old_protection = 0;
    CHECK(VirtualProtect(page, page_size, PAGE_READONLY, &old_protection) != FALSE);
    const auto installed = fair_transmog::mem::ReplacePointer(page, first, second);
    CHECK(installed.installed);
    CHECK(installed.protection_restored);
    CHECK(*page == second);

    MEMORY_BASIC_INFORMATION information{};
    CHECK(VirtualQuery(page, &information, sizeof(information)) == sizeof(information));
    CHECK((information.Protect & 0xffU) == PAGE_READONLY);

    const auto conflict = fair_transmog::mem::ReplacePointer(page, first, third);
    CHECK(!conflict.installed);
    CHECK(conflict.protection_restored);
    CHECK(*page == second);

    VirtualFree(page, 0, MEM_RELEASE);
}

}  // namespace

int main() {
    TestRejectedAccess();
    TestReadWriteAndBounds();
    TestExecutablePointerRejected();
    TestReplacePointer();
    return g_failures == 0 ? 0 : 1;
}
