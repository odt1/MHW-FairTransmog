#include "game/layered_menu.h"

#include "runtime/log.h"
#include "runtime/memory.h"

#include <windows.h>

#include <intrin.h>

#include <MinHook.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace fair_transmog::game::layered_menu {
namespace {

// RVAs in the unpacked image of the packed build profile::kKnownPackedExecutableSha256
// (re/PROTOTYPE_MILESTONE.md). The builder's catalog getter, (index < 0x2F0) -> set record or null.
constexpr std::uintptr_t kCatalogAtRva = 0x16721D0;
// First nonzero part type of a set record: 0 ordinary, 1 full-body ordinary, 2 layered,
// 3 full-body layered (the menu applies those through a confirmation, all five slots at once).
constexpr std::uintptr_t kSetTypeRva = 0x1672230;
constexpr std::uint8_t kOrdinary = 0;
constexpr std::uint8_t kFullBodyOrdinary = 1;
constexpr std::uint8_t kLayered = 2;
constexpr std::uint8_t kFullBodyLayered = 3;
// Layered unlock check (user, set ID, slot 0-4, or 6 for any piece).
constexpr std::uintptr_t kAvailableRva = 0x134CE60;
constexpr std::uintptr_t kUserSingletonRva = 0x5013950;
constexpr std::uintptr_t kEquipMetadataRva = 0x5011F00;

// The layered list builder sub_140525370, and the block of uGUIEquipOuter methods (layered list,
// selection, piece display) that call the availability check.
constexpr std::uintptr_t kBuilderBegin = 0x525370;
constexpr std::uintptr_t kBuilderEnd = 0x5256AB;
constexpr std::uintptr_t kMenuBegin = 0x523A50;
constexpr std::uintptr_t kMenuEnd = 0x5266AF;

constexpr std::array<std::uint8_t, 17> kCatalogAtPrologue{
    0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0x27, 0xFD, 0x99, 0x03, 0x48, 0x81, 0xC1, 0xE8, 0x27, 0x01, 0x00, 0xE9,
};
constexpr std::array<std::uint8_t, 19> kSetTypePrologue{
    0x33, 0xC0, 0x48, 0x83, 0xC1, 0x18, 0x48, 0x8B, 0x11, 0x48, 0x85, 0xD2, 0x74, 0x0A, 0x44, 0x0F, 0xB6, 0x42, 0x13,
};
constexpr std::array<std::uint8_t, 23> kAvailablePrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55, 0x48,
    0x8D, 0x6C, 0x24, 0xE0, 0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00,
};

// Selected character: slot at +0xA0 into the user array at +0xA8. The native layered selection
// writes the saved set IDs under the critical section at user+0x8.
constexpr std::ptrdiff_t kSelectedSlot = 0xA0;
constexpr std::ptrdiff_t kUserArray = 0xA8;
constexpr std::ptrdiff_t kUserStride = 0x26CC00;
constexpr std::ptrdiff_t kUserLock = 0x8;
constexpr std::ptrdiff_t kSavedLayered = 0xE7434;
constexpr std::ptrdiff_t kPlayerActorArmor = 0x13754;
// Equipment box: inline entries sharing one vtable; armor has type 0, a slot and a per-slot key.
constexpr std::ptrdiff_t kBox = 0x41068;
constexpr std::size_t kBoxEntries = 4260;
constexpr std::size_t kBoxEntrySize = 0x98;
constexpr std::ptrdiff_t kEntryType = 0x18;
constexpr std::ptrdiff_t kEntrySlot = 0x20;
constexpr std::ptrdiff_t kEntryKey = 0x24;
// Set records, as in transmog.cpp; a part's +0x1E is the per-slot key the box and renderer use.
constexpr std::ptrdiff_t kSetTable = 0x24190;
constexpr std::size_t kSetStride = 0x90;
constexpr std::ptrdiff_t kSetCategory = 0x0;
constexpr std::ptrdiff_t kSetId = 0x4;
constexpr std::ptrdiff_t kSetParts = 0x18;
constexpr std::size_t kSetPartCount = 6;
constexpr std::ptrdiff_t kPartType = 0x13;
constexpr std::ptrdiff_t kPartKey = 0x1E;
constexpr std::int32_t kSetLimit = (0x41790 - kSetTable) / kSetStride;
// Per-slot keys are bounds-checked against 0x2EF by the native row lookup (RVA 0x279740).
constexpr std::size_t kRowKeys = 0x2EF;
constexpr std::size_t kSlots = 5;
constexpr std::int32_t kAnyPiece = 6;
constexpr std::array<const char*, kSlots> kSlotNames{"Head", "Chest", "Arms", "Waist", "Legs"};

// Owned armor keys per slot, from the equipment box.
using Ownership = std::array<std::bitset<kRowKeys>, kSlots>;

using CatalogAtFn = void*(__fastcall*)(int index);
using SetTypeFn = std::uint64_t(__fastcall*)(const void* record);
using AvailableFn = std::uint64_t(__fastcall*)(void* user, int set, int slot);

// The record holds pointers, so the copy keeps their 8-byte alignment.
struct SetCopy {
    std::array<std::uint64_t, kSetStride / sizeof(std::uint64_t)> words;
    std::uint8_t type = kOrdinary;

    std::uint8_t* data() noexcept { return reinterpret_cast<std::uint8_t*>(words.data()); }
    const std::uint8_t* data() const noexcept { return reinterpret_cast<const std::uint8_t*>(words.data()); }
};

std::uint8_t* g_exe = nullptr;
bool g_installed = false;
CatalogAtFn g_catalog_at = nullptr;
SetTypeFn g_set_type = nullptr;
AvailableFn g_available = nullptr;
// Ownership snapshot taken by the list builder (main thread); menu selections read it on job
// threads. OnRefresh takes its own snapshot instead.
SRWLOCK g_owned_lock = SRWLOCK_INIT;
Ownership g_owned;
// Copies of owned ordinary set records fed to the builder in place of empty catalog entries,
// with the category clamped to the two layered tabs. Only the builder loop, on the main thread,
// touches them.
std::vector<SetCopy> g_fair_sets;
std::size_t g_next_fair = 0;
std::size_t g_logged_offer = SIZE_MAX;
bool g_logged_overflow = false;
// Diagnostic builds only: sets treated as owned in every slot, for testing without save edits.
// Written once by Install before the hooks are enabled.
std::vector<std::int32_t> g_test_owned_sets;

template <typename... Args>
void SafeLog(std::format_string<Args...> format, Args&&... args) noexcept {
    try {
        log::Log(format, std::forward<Args>(args)...);
    } catch (...) {
        // Logging must never unwind through game code.
    }
}

bool CalledFrom(const void* return_address, std::uintptr_t begin, std::uintptr_t end) noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(return_address);
    const auto exe = reinterpret_cast<std::uintptr_t>(g_exe);
    return address >= exe + begin && address < exe + end;
}

std::uint8_t* SelectedUser() noexcept {
    auto* singleton = mem::Read<std::uint8_t*>(g_exe + kUserSingletonRva).value_or(nullptr);
    if (singleton == nullptr) {
        return nullptr;
    }
    const auto slot = mem::Read<std::int32_t>(singleton + kSelectedSlot);
    auto* users = mem::Read<std::uint8_t*>(singleton + kUserArray).value_or(nullptr);
    if (!slot || *slot < 0 || users == nullptr) {
        return nullptr;
    }
    return users + *slot * kUserStride;
}

const std::uint8_t* SetRecord(std::int32_t set) noexcept {
    if (set < 0 || set >= kSetLimit) {
        return nullptr;
    }
    auto* equipment = mem::Read<std::uint8_t*>(g_exe + kEquipMetadataRva).value_or(nullptr);
    if (equipment == nullptr) {
        return nullptr;
    }
    const auto* record = equipment + kSetTable + set * kSetStride;
    return mem::Read<std::int32_t>(record + kSetId) == set ? record : nullptr;
}

const std::uint8_t* Part(const std::uint8_t* record, std::size_t slot) noexcept {
    return mem::Read<const std::uint8_t*>(record + kSetParts + slot * sizeof(void*)).value_or(nullptr);
}

// Same rule as the native set-type function: the first part with a nonzero type decides.
std::uint8_t SetType(const std::uint8_t* record) noexcept {
    for (std::size_t slot = 0; slot != kSetPartCount; ++slot) {
        if (const auto* part = Part(record, slot)) {
            const auto type = mem::Read<std::uint8_t>(part + kPartType).value_or(0);
            if (type != 0) {
                return type;
            }
        }
    }
    return 0;
}

bool IsOrdinary(std::uint8_t type) noexcept {
    return type == kOrdinary || type == kFullBodyOrdinary;
}

bool Owns(const Ownership& owned, const std::uint8_t* record, std::size_t slot) noexcept {
    const auto* part = Part(record, slot);
    if (part == nullptr) {
        return false;
    }
    const auto key = mem::Read<std::uint16_t>(part + kPartKey);
    return key && *key < kRowKeys && owned[slot][*key];
}

bool OwnsAny(const Ownership& owned, const std::uint8_t* record) noexcept {
    for (std::size_t slot = 0; slot != kSlots; ++slot) {
        if (Owns(owned, record, slot)) {
            return true;
        }
    }
    return false;
}

// Returns the number of armor pieces found; zero also when the box could not be read.
std::size_t ScanBox(Ownership& owned) {
    for (auto& slot : owned) {
        slot.reset();
    }
    auto* user = SelectedUser();
    if (user == nullptr) {
        return 0;
    }
    std::vector<std::uint8_t> box(kBoxEntries * kBoxEntrySize);
    if (!mem::Read(user + kBox, box.data(), box.size())) {
        return 0;
    }
    std::size_t pieces = 0;
    const auto read = [&](std::size_t entry, std::ptrdiff_t offset, auto value) {
        std::memcpy(&value, box.data() + entry * kBoxEntrySize + offset, sizeof(value));
        return value;
    };
    const auto vtable = read(0, 0, std::uintptr_t{});
    for (std::size_t i = 0; i != kBoxEntries && read(i, 0, std::uintptr_t{}) == vtable; ++i) {
        const auto slot = read(i, kEntrySlot, std::int32_t{});
        const auto key = read(i, kEntryKey, std::int32_t{});
        if (read(i, kEntryType, std::int32_t{}) == 0 && slot >= 0 && slot < static_cast<std::int32_t>(kSlots) &&
            key >= 0 && key < static_cast<std::int32_t>(kRowKeys)) {
            owned[slot][key] = true;
            ++pieces;
        }
    }
    for (const auto set : g_test_owned_sets) {
        const auto* record = SetRecord(set);
        for (std::size_t slot = 0; record != nullptr && slot != kSlots; ++slot) {
            const auto* part = Part(record, slot);
            const auto key = part ? mem::Read<std::uint16_t>(part + kPartKey) : std::nullopt;
            if (key && *key < kRowKeys) {
                owned[slot][*key] = true;
                ++pieces;
            }
        }
    }
    return pieces;
}

void CollectFairSets() {
    Ownership owned;
    const std::size_t pieces = ScanBox(owned);
    AcquireSRWLockExclusive(&g_owned_lock);
    g_owned = owned;
    ReleaseSRWLockExclusive(&g_owned_lock);
    g_fair_sets.clear();
    for (std::int32_t set = 0; set != kSetLimit; ++set) {
        const auto* record = SetRecord(set);
        const auto type = record ? SetType(record) : kLayered;
        if (!IsOrdinary(type) || !OwnsAny(owned, record)) {
            continue;
        }
        SetCopy copy{};
        copy.type = type;
        if (!mem::Read(record, copy.data(), kSetStride)) {
            continue;
        }
        // Tab 0 holds low/high rank layered sets, tab 1 master rank ones; the builder has no third tab.
        std::uint32_t category = 0;
        std::memcpy(&category, copy.data() + kSetCategory, sizeof(category));
        category = category == 0 ? 0 : 1;
        std::memcpy(copy.data() + kSetCategory, &category, sizeof(category));
        g_fair_sets.push_back(copy);
    }
    if (g_fair_sets.size() != g_logged_offer) {
        g_logged_offer = g_fair_sets.size();
        SafeLog("Layered menu: offering {} owned ordinary armor sets ({} owned armor pieces in the box).",
                g_fair_sets.size(), pieces);
    }
}

const SetCopy* FairCopy(const void* record) noexcept {
    for (const auto& copy : g_fair_sets) {
        if (copy.data() == record) {
            return &copy;
        }
    }
    return nullptr;
}

void* __fastcall CatalogAtDetour(int index) {
    void* record = g_catalog_at(index);
    if (!CalledFrom(_ReturnAddress(), kBuilderBegin, kBuilderEnd)) {
        return record;
    }
    if (index == 0) {
        try {
            CollectFairSets();
        } catch (...) {
            g_fair_sets.clear();
        }
        g_next_fair = 0;
    }
    if (record == nullptr && g_next_fair < g_fair_sets.size()) {
        return g_fair_sets[g_next_fair++].data();
    }
    if (index == 0x2EF && g_next_fair < g_fair_sets.size() && !g_logged_overflow) {
        g_logged_overflow = true;
        SafeLog("Layered menu: {} owned sets did not fit the list's free entries.", g_fair_sets.size() - g_next_fair);
    }
    return record;
}

std::uint64_t __fastcall SetTypeDetour(const void* record) {
    const void* caller = _ReturnAddress();
    // Copies are listed like layered sets; only the builder (main thread, like their owner) sees them.
    if (CalledFrom(caller, kBuilderBegin, kBuilderEnd)) {
        if (const auto* copy = FairCopy(record)) {
            return copy->type == kFullBodyOrdinary ? kFullBodyLayered : kLayered;
        }
    }
    const auto type = g_set_type(record);
    // The menu's selection applies a set through the full-body path only when it reports type 3.
    if (type == kFullBodyOrdinary && CalledFrom(caller, kMenuBegin, kMenuEnd)) {
        return kFullBodyLayered;
    }
    return type;
}

std::uint64_t __fastcall AvailableDetour(void* user, int set, int slot) {
    if (CalledFrom(_ReturnAddress(), kMenuBegin, kMenuEnd)) {
        const auto* record = SetRecord(set);
        if (record != nullptr && IsOrdinary(SetType(record))) {
            AcquireSRWLockShared(&g_owned_lock);
            const bool owned = slot == kAnyPiece ? OwnsAny(g_owned, record)
                                                 : slot >= 0 && slot < static_cast<int>(kSlots) &&
                                                       Owns(g_owned, record, static_cast<std::size_t>(slot));
            ReleaseSRWLockShared(&g_owned_lock);
            return owned;
        }
    }
    return g_available(user, set, slot);
}

}  // namespace

bool Install(const std::filesystem::path& config_path) {
    g_exe = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
#if FAIR_TRANSMOG_DIAGNOSTICS
    wchar_t text[256]{};
    GetPrivateProfileStringW(L"Diagnostics", L"OwnedSets", L"", text, static_cast<DWORD>(std::size(text)),
                             config_path.c_str());
    for (wchar_t* p = text; *p != L'\0';) {
        wchar_t* end = nullptr;
        const long set = std::wcstol(p, &end, 10);
        if (end == p) {
            ++p;
            continue;
        }
        if (set >= 0 && set < kSetLimit) {
            g_test_owned_sets.push_back(static_cast<std::int32_t>(set));
        }
        p = end;
    }
    if (!g_test_owned_sets.empty()) {
        log::Log("Layered menu: diagnostic build treats {} configured set(s) as owned.", g_test_owned_sets.size());
    }
#else
    (void)config_path;
#endif
    auto* catalog_at = g_exe + kCatalogAtRva;
    auto* set_type = g_exe + kSetTypeRva;
    auto* available = g_exe + kAvailableRva;
    if (!mem::Matches(catalog_at, kCatalogAtPrologue) || !mem::Matches(set_type, kSetTypePrologue) ||
        !mem::Matches(available, kAvailablePrologue)) {
        log::Write("Layered menu: code does not match the known build; owned armor not offered.");
        return false;
    }

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        log::Write("Layered menu: MinHook initialization failed; owned armor not offered.");
        return false;
    }
    const std::array<std::pair<void*, std::pair<void*, void**>>, 3> hooks{{
        {catalog_at, {reinterpret_cast<void*>(&CatalogAtDetour), reinterpret_cast<void**>(&g_catalog_at)}},
        {set_type, {reinterpret_cast<void*>(&SetTypeDetour), reinterpret_cast<void**>(&g_set_type)}},
        {available, {reinterpret_cast<void*>(&AvailableDetour), reinterpret_cast<void**>(&g_available)}},
    }};
    std::size_t created = 0;
    MH_STATUS status = MH_OK;
    for (; created != hooks.size(); ++created) {
        const auto& [target, detour] = hooks[created];
        status = MH_CreateHook(target, detour.first, detour.second);
        if (status != MH_OK) {
            break;
        }
    }
    if (status == MH_OK) {
        for (const auto& [target, detour] : hooks) {
            if ((status = MH_EnableHook(target)) != MH_OK) {
                break;
            }
        }
    }
    if (status != MH_OK) {
        for (std::size_t i = 0; i != created; ++i) {
            MH_RemoveHook(hooks[i].first);
        }
        log::Log("Layered menu: hooks failed ({}); owned armor not offered.", MH_StatusToString(status));
        return false;
    }
    g_installed = true;
    log::Write("Layered menu: owned ordinary armor is offered in Layered Armor Settings (fair path).");
    return true;
}

void OnRefresh(std::uint8_t* player) noexcept {
    if (!g_installed || player == nullptr) {
        return;
    }
    try {
        Ownership owned;
        auto* user = SelectedUser();
        // An unreadable or not yet loaded box must never be mistaken for "owns nothing".
        if (user == nullptr || ScanBox(owned) == 0) {
            return;
        }
        auto* saved = reinterpret_cast<std::int32_t*>(user + kSavedLayered);
        auto* section = reinterpret_cast<CRITICAL_SECTION*>(user + kUserLock);
        for (std::size_t slot = 0; slot != kSlots; ++slot) {
            const auto set = mem::Read<std::int32_t>(saved + slot);
            if (!set || *set < 0) {
                continue;
            }
            const auto* record = SetRecord(*set);
            if (record == nullptr || !IsOrdinary(SetType(record)) || Owns(owned, record, slot)) {
                continue;
            }
            // Same writes as the native selection when it unequips a layered piece.
            EnterCriticalSection(section);
            if (saved[slot] == *set) {
                saved[slot] = -1;
            }
            LeaveCriticalSection(section);
            const std::int32_t none = -1;
            mem::Write(player + kPlayerActorArmor + slot * sizeof(none), &none, sizeof(none));
            SafeLog("Layered menu: cleared {} set {} from layered armor; the character no longer owns it.",
                    kSlotNames[slot], *set);
        }
    } catch (...) {
        // Ownership scanning can fail to allocate; the refresh then proceeds unchanged.
    }
}

}  // namespace fair_transmog::game::layered_menu
