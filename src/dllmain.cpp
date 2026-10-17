#include <windows.h>

#include "game/appearance.h"

#if !defined(_M_X64) || defined(_M_ARM64EC)
#error MHW FairTransmog requires an x64 MSVC target.
#endif

namespace {

DWORD WINAPI Bootstrap(void* parameter) {
    fair_transmog::game::Start(static_cast<HMODULE>(parameter));
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    HANDLE thread = CreateThread(nullptr, 0, Bootstrap, module, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
    return TRUE;
}
