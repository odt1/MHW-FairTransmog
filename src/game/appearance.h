#pragma once

#include <windows.h>

namespace fair_transmog::game {

// Bootstrap runs on the DLL's worker; game calls are deferred to the game thread.
void Start(HMODULE module);

}  // namespace fair_transmog::game
