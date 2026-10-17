#pragma once

#include <filesystem>

namespace fair_transmog::game::transmog {

// Direct override ("cheat" mode) for the known packed build only: shows any armor set per slot,
// from set IDs in `config_path`, regardless of ownership and on top of the layered-menu choice.
// Off (-1) by default. It overrides the local player's actor armor values on every native model
// refresh; saved layered IDs are never written. Returns false and leaves the game untouched when
// the expected code is not found.
bool Install(const std::filesystem::path& config_path);

}  // namespace fair_transmog::game::transmog
