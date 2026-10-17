#pragma once

#include <cstdint>
#include <filesystem>

namespace fair_transmog::game::layered_menu {

// Fair path for the known packed build only: offers the selected character's owned ordinary armor
// sets (full-body ones as full-body layered sets) in the native Layered Armor list, with per-piece
// availability from the equipment box. The game's own menu code lists, applies and saves the
// choice. Diagnostic builds also read test-only owned sets from `config_path`. Returns false and
// leaves the game untouched when the expected code is not found.
bool Install(const std::filesystem::path& config_path);

// Called before each native model refresh of the local player, on whatever thread runs it: clears
// saved layered slots holding ordinary armor the character no longer owns (sold, or set by a
// loadout). Does nothing when not installed or when the equipment box cannot be read.
void OnRefresh(std::uint8_t* player) noexcept;

}  // namespace fair_transmog::game::layered_menu
