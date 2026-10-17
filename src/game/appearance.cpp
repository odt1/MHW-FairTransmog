#include "game/appearance.h"

#include "game/layered_menu.h"
#include "game/transmog.h"
#include "runtime/log.h"
#include "runtime/profile.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

namespace fair_transmog::game {
namespace {

constexpr char kFailClosed[] =
    "Fail closed: nothing installed; no game calls, hooks, or game-memory writes.";

std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void ReportBootstrapFailure(const char* reason) noexcept {
    // Logging can fail before its file is open, or throw while reporting an exception.
    OutputDebugStringA("[MHW-FairTransmog] ");
    OutputDebugStringA(reason);
    OutputDebugStringA("\n");
    OutputDebugStringA(kFailClosed);
    OutputDebugStringA("\n");
    try {
        log::Write(reason);
        log::Write(kFailClosed);
    } catch (...) {
        // Never propagate an exception across the bootstrap thread's Win32 boundary.
    }
}

}  // namespace

void Start(HMODULE module) {
    try {
        if (module == nullptr) {
            ReportBootstrapFailure("Bootstrap received a null plugin module handle.");
            return;
        }

        std::string error;
        const auto plugin_path = profile::ModuleDiskPath(module, error);
        if (!plugin_path) {
            ReportBootstrapFailure(error.c_str());
            return;
        }
        auto log_path = *plugin_path;
        log_path.replace_extension(L".log");
        if (!log::Initialize(log_path)) {
            ReportBootstrapFailure("Could not initialize the plugin log file.");
            return;
        }

        log::Log("MHW FairTransmog {} starting.", FAIR_TRANSMOG_VERSION);
        log::Log("Plugin module: path='{}', base=0x{:016X}.", PathUtf8(*plugin_path),
                 reinterpret_cast<std::uintptr_t>(module));

        const auto host = profile::InspectHostExecutable(error);
        if (!host) {
            ReportBootstrapFailure(error.c_str());
            return;
        }
        log::Log("Host module: path='{}', base=0x{:016X}, disk bytes={}.", PathUtf8(host->disk_path),
                 reinterpret_cast<std::uintptr_t>(host->module), host->disk_size);
        log::Log("Host executable SHA-256 (disk file, not unpacked loaded memory): {}.",
                 host->disk_sha256);

        if (host->disk_sha256 != profile::kKnownPackedExecutableSha256) {
            log::Log("Unsupported game executable; this version supports SHA-256 {} only.",
                     profile::kKnownPackedExecutableSha256);
        } else {
            log::Write("Supported game executable.");
            auto config_path = *plugin_path;
            config_path.replace_extension(L".ini");
            const bool direct = transmog::Install(config_path);
            const bool fair = layered_menu::Install(config_path);
            if (direct || fair) {
                return;
            }
        }
        log::Write(kFailClosed);
    } catch (const std::exception& exception) {
        ReportBootstrapFailure(exception.what());
    } catch (...) {
        ReportBootstrapFailure("Unknown exception during bootstrap.");
    }
}

}  // namespace fair_transmog::game
