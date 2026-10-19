#include "game/transmog.h"

#include "game/layered_menu.h"
#include "runtime/log.h"
#include "runtime/memory.h"

#include <windows.h>

#include <intrin.h>

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

namespace fair_transmog::game::transmog {
namespace {

// RVAs in the unpacked image of the packed build profile::kKnownPackedExecutableSha256.
// sTicketManager's per-frame update. sMhMain's update (+0x1AF2B60) calls it first, on the main
// thread, every frame, through this vtable slot only. sMhMain's update itself is contested: MHW-DLSS4
// swaps its vtable slot and other plugins patch its code (re/PROTOTYPE_MILESTONE.md).
constexpr std::uintptr_t kFrameTickRva = 0x1B80F30;
constexpr std::uintptr_t kFrameTickSlotRva = 0x3404AB0;
// Player model refresh, (player, flag, flag); the native layered menu and area loads call it.
constexpr std::uintptr_t kRefreshRva = 0x2041700;
constexpr std::uintptr_t kPlayerManagerRva = 0x500ECA0;
constexpr std::uintptr_t kEquipMetadataRva = 0x5011F00;

constexpr std::array<std::uint8_t, 32> kRefreshPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
    0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x81, 0x1C, 0x26, 0x01, 0x00,
};
// push rdi; sub rsp, 0x30. Another plugin may have patched these six bytes with a jump; the slot hook
// does not touch the code, so only the bytes after them must match.
constexpr std::array<std::uint8_t, 6> kFrameTickPrologue{0x40, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::ptrdiff_t kFrameTickBodyOffset = 6;
constexpr std::array<std::uint8_t, 10> kFrameTickBody{0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x8B, 0xF9, 0x48, 0x89};

// Player manager: 24 player records; the local one carries the manager's owner value.
constexpr std::ptrdiff_t kManagerOwner = 0xAE40;
constexpr std::size_t kPlayerRecords = 24;
constexpr std::ptrdiff_t kPlayerRecordStride = 0x740;
constexpr std::ptrdiff_t kRecordOwner = 0x48;
constexpr std::ptrdiff_t kRecordPlayer = 0x58;
// Five u32 actor armor values the model refresh renders (not the saved layered set IDs).
constexpr std::ptrdiff_t kPlayerActorArmor = 0x13754;

// Equipment metadata: one 0x90-byte record per armor set ID with a part pointer per slot.
constexpr std::ptrdiff_t kSetTable = 0x24190;
constexpr std::ptrdiff_t kSetStride = 0x90;
constexpr std::ptrdiff_t kSetId = 0x4;
constexpr std::ptrdiff_t kSetParts = 0x18;
constexpr std::ptrdiff_t kPartActor = 0x1E;
// Another table starts at +0x41790, so set records cannot extend past it.
constexpr std::int32_t kSetLimit = (0x41790 - kSetTable) / kSetStride;

constexpr std::size_t kSlots = 5;
constexpr std::array<const wchar_t*, kSlots> kSlotKeys{L"Head", L"Chest", L"Arms", L"Waist", L"Legs"};
constexpr wchar_t kAllKey[] = L"All";
constexpr ULONGLONG kConfigCheckMs = 1000;

constexpr char kDefaultConfig[] =
    "; MHW FairTransmog: direct override (\"cheat\" mode)\n"
    ";\n"
    "; Shows any armor set, owned or not, on top of whatever the Layered Armor menu shows.\n"
    "; Values are armor set IDs; ARMOR_IDS.md lists them all (for example 480).\n"
    "; All sets every slot at once; a slot with its own ID uses that one instead.\n"
    "; -1 or empty = off, the game's own look is shown. Changes apply within a second in game.\n"
    "[Transmog]\n"
    "All=-1\n"
    "Head=-1\n"
    "Chest=-1\n"
    "Arms=-1\n"
    "Waist=-1\n"
    "Legs=-1\n"
    ";\n"
    "; SuperCheat=1 lists every armor set in the Layered Armor menu so you can preview and select any\n"
    "; piece, owned or not. With SuperCheat=0 ordinary armor you do not own is removed again at the next refresh.\n"
    "SuperCheat=0\n";

using FrameTickFn = void*(__fastcall*)(void* self);
using RefreshFn = void*(__fastcall*)(void* player, int flag, int flag2);

struct Slot {
    std::int32_t set = -1;
    bool applied = false;
    std::uint32_t applied_value = 0;
    std::uint32_t native_value = 0;
};

std::uint8_t* g_exe = nullptr;
std::filesystem::path g_config_path;
FrameTickFn g_frame_tick = nullptr;
RefreshFn g_refresh = nullptr;
RefreshFn g_refresh_entry = nullptr;
// The frame tick owns the config fields; refreshes also arrive from other threads (layered-menu
// selections run on job threads, a load from the title screen on another), so slot and player
// state is guarded.
SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<DWORD> g_main_thread{0};
std::array<Slot, kSlots> g_slots;
std::uint8_t* g_player = nullptr;
FILETIME g_config_time{};
ULONGLONG g_next_config_check = 0;
bool g_pending = false;
bool g_reported_unresolved = false;
std::atomic<bool> g_reported_thread{false};

template <typename... Args>
void SafeLog(std::format_string<Args...> format, Args&&... args) noexcept {
    try {
        log::Log(format, std::forward<Args>(args)...);
    } catch (...) {
        // Logging must never unwind through game code.
    }
}

std::string Join(const std::array<std::uint32_t, kSlots>& values) {
    return std::format("{},{},{},{},{}", values[0], values[1], values[2], values[3], values[4]);
}

std::uint8_t* LocalPlayer() noexcept {
    auto* manager = mem::Read<std::uint8_t*>(g_exe + kPlayerManagerRva).value_or(nullptr);
    if (manager == nullptr) {
        return nullptr;
    }
    const auto owner = mem::Read<std::uint64_t>(manager + kManagerOwner);
    if (!owner) {
        return nullptr;
    }
    for (std::size_t i = 0; i != kPlayerRecords; ++i) {
        auto* record = manager + i * kPlayerRecordStride;
        if (mem::Read<std::uint64_t>(record + kRecordOwner) == owner) {
            return mem::Read<std::uint8_t*>(record + kRecordPlayer).value_or(nullptr);
        }
    }
    return nullptr;
}

std::optional<std::uint32_t> ActorValue(std::int32_t set, std::size_t slot) noexcept {
    if (set < 0 || set >= kSetLimit) {
        return std::nullopt;
    }
    auto* equipment = mem::Read<std::uint8_t*>(g_exe + kEquipMetadataRva).value_or(nullptr);
    if (equipment == nullptr) {
        return std::nullopt;
    }
    auto* record = equipment + kSetTable + set * kSetStride;
    if (mem::Read<std::uint32_t>(record + kSetId) != static_cast<std::uint32_t>(set)) {
        return std::nullopt;
    }
    auto* part = mem::Read<std::uint8_t*>(record + kSetParts + slot * sizeof(void*)).value_or(nullptr);
    if (part == nullptr) {
        return std::nullopt;
    }
    return mem::Read<std::uint16_t>(part + kPartActor);
}

// Runs before every refresh of the local player: the game has just written its own values, so
// remember them per slot, then put the override (or the remembered native value) in place.
void ApplyOverride(std::uint8_t* player) noexcept {
    if (player != g_player) {
        g_player = player;
        for (auto& slot : g_slots) {
            slot.applied = false;
        }
    }

    auto* armor = reinterpret_cast<std::uint32_t*>(player + kPlayerActorArmor);
    std::array<std::uint32_t, kSlots> before{};
    std::array<std::uint32_t, kSlots> after{};
    for (std::size_t i = 0; i != kSlots; ++i) {
        const auto current = mem::Read<std::uint32_t>(armor + i);
        if (!current) {
            return;
        }
        before[i] = *current;
    }

    bool unresolved = false;
    for (std::size_t i = 0; i != kSlots; ++i) {
        auto& slot = g_slots[i];
        if (!slot.applied || before[i] != slot.applied_value) {
            slot.native_value = before[i];
        }
        const auto wanted = ActorValue(slot.set, i);
        unresolved |= slot.set >= 0 && !wanted;
        after[i] = wanted.value_or(slot.native_value);
        if (after[i] != before[i] && !mem::Write(armor + i, &after[i], sizeof(after[i]))) {
            after[i] = before[i];
            continue;
        }
        slot.applied = wanted.has_value();
        slot.applied_value = after[i];
    }

    if (unresolved && !g_reported_unresolved) {
        g_reported_unresolved = true;
        SafeLog("Some configured set IDs have no part for their slot; those slots keep the game's value.");
    }
    if (before != after) {
        try {
            SafeLog("Refresh override: [{}] -> [{}].", Join(before), Join(after));
        } catch (...) {
            // Join can fail to allocate; the override itself is already in place.
        }
    }
}

std::int32_t ReadSlotSet(const wchar_t* key) noexcept {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"Transmog", key, L"", text, static_cast<DWORD>(std::size(text)),
                             g_config_path.c_str());
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (end == text || value < 0 || value >= kSetLimit) {
        return -1;
    }
    return static_cast<std::int32_t>(value);
}

// True when the file's write time changed since the last load and the slots were reloaded.
bool ReloadConfig() noexcept {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    FILETIME time{};
    if (GetFileAttributesExW(g_config_path.c_str(), GetFileExInfoStandard, &attributes)) {
        time = attributes.ftLastWriteTime;
    }
    if (CompareFileTime(&time, &g_config_time) == 0) {
        return false;
    }
    g_config_time = time;
    // A slot's own ID wins over All.
    const std::int32_t all = ReadSlotSet(kAllKey);
    std::array<std::int32_t, kSlots> sets{};
    for (std::size_t i = 0; i != kSlots; ++i) {
        const std::int32_t own = ReadSlotSet(kSlotKeys[i]);
        sets[i] = own >= 0 ? own : all;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (std::size_t i = 0; i != kSlots; ++i) {
        g_slots[i].set = sets[i];
    }
    g_reported_unresolved = false;
    ReleaseSRWLockExclusive(&g_lock);
    SafeLog("Config: All={}; in effect Head={} Chest={} Arms={} Waist={} Legs={}.", all, sets[0], sets[1],
            sets[2], sets[3], sets[4]);
    return true;
}

void* __fastcall RefreshDetour(void* player, int flag, int flag2) {
    if (player != nullptr && player == LocalPlayer()) {
        const DWORD thread = GetCurrentThreadId();
        const DWORD main_thread = g_main_thread.load(std::memory_order_relaxed);
        if (thread != main_thread && !g_reported_thread.exchange(true)) {
            SafeLog("Refresh on thread {} (main thread {}), called from +0x{:X}; further ones not logged.",
                    thread, main_thread,
                    reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - reinterpret_cast<std::uintptr_t>(g_exe));
        }
        // The fair path first drops pieces no longer owned; the direct override then wins.
        layered_menu::OnRefresh(static_cast<std::uint8_t*>(player));
        AcquireSRWLockExclusive(&g_lock);
        ApplyOverride(static_cast<std::uint8_t*>(player));
        ReleaseSRWLockExclusive(&g_lock);
    }
    return g_refresh(player, flag, flag2);
}

void* __fastcall FrameTickDetour(void* self) {
    g_main_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const ULONGLONG now = GetTickCount64();
    if (now >= g_next_config_check) {
        g_next_config_check = now + kConfigCheckMs;
        g_pending |= ReloadConfig();
    }
    if (g_pending) {
        AcquireSRWLockShared(&g_lock);
        auto* player = g_player;
        ReleaseSRWLockShared(&g_lock);
        // Re-render only a player the game has already refreshed itself, never a half-built one.
        if (player != nullptr && LocalPlayer() == player) {
            g_pending = false;
            g_refresh_entry(player, 1, 0);
        }
    }
    return g_frame_tick(self);
}

bool WriteDefaultConfig() {
    std::ofstream file(g_config_path, std::ios::binary);
    file << kDefaultConfig;
    return static_cast<bool>(file);
}

}  // namespace

bool Install(const std::filesystem::path& config_path) {
    g_exe = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    g_config_path = config_path;

    auto* refresh = g_exe + kRefreshRva;
    auto* frame_tick = g_exe + kFrameTickRva;
    if (!mem::Matches(refresh, kRefreshPrologue)) {
        log::Write("Transmog: model refresh code does not match the known build; not installed.");
        return false;
    }
    const bool frame_tick_patched = mem::Read<std::uint8_t>(frame_tick) == std::uint8_t{0xE9};
    if (!mem::Matches(frame_tick + kFrameTickBodyOffset, kFrameTickBody) ||
        !(frame_tick_patched || mem::Matches(frame_tick, kFrameTickPrologue))) {
        log::Write("Transmog: frame tick code does not match the known build; not installed.");
        return false;
    }

    std::error_code error;
    if (!std::filesystem::exists(g_config_path, error) && !WriteDefaultConfig()) {
        log::Write("Transmog: could not create the default config; continuing without it.");
    }
    ReloadConfig();

    if (MH_Initialize() != MH_OK) {
        log::Write("Transmog: MinHook initialization failed; not installed.");
        return false;
    }
    const MH_STATUS refresh_status =
        MH_CreateHook(refresh, reinterpret_cast<void*>(&RefreshDetour), reinterpret_cast<void**>(&g_refresh));
    g_refresh_entry = reinterpret_cast<RefreshFn>(refresh);
    const MH_STATUS enable_status = refresh_status == MH_OK ? MH_EnableHook(MH_ALL_HOOKS) : MH_UNKNOWN;
    if (enable_status != MH_OK) {
        log::Log("Transmog: hooks failed (refresh {}, enable {}); not installed.", MH_StatusToString(refresh_status),
                 MH_StatusToString(enable_status));
        MH_Uninitialize();
        return false;
    }
    // A vtable slot swap chains with code hooks in any order and with other slot hooks, including
    // one installed at the same moment. The detour can run as soon as the slot points at it, so the
    // function it calls is stored first.
    const auto patch = mem::HookPointer(reinterpret_cast<void**>(g_exe + kFrameTickSlotRva),
                                        reinterpret_cast<void*>(&FrameTickDetour),
                                        reinterpret_cast<void**>(&g_frame_tick));
    if (!patch.installed) {
        log::Write("Transmog: could not hook the frame tick vtable slot; not installed.");
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        return false;
    }
    if (!patch.protection_restored) {
        log::Write("Transmog: the frame tick vtable page kept write access after the hook.");
    }
    const bool chained = reinterpret_cast<std::uint8_t*>(g_frame_tick) != frame_tick;
    log::Log("Transmog: direct override hooks installed (frame tick slot +0x{:X}{}, refresh +0x{:X}); "
             "saved layered IDs are never written.",
             kFrameTickSlotRva, chained ? ", chained after another plugin's hook" : "", kRefreshRva);
    return true;
}

}  // namespace fair_transmog::game::transmog
