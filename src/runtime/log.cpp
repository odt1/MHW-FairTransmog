#include "runtime/log.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <share.h>
#include <system_error>

namespace fair_transmog::log {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

}  // namespace

bool Initialize(const std::filesystem::path& path) {
    std::lock_guard lock(g_mutex);
    if (g_file != nullptr) {
        return true;
    }

    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    // A fresh log per game launch keeps it small and about the current session.
    g_file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
    return g_file != nullptr;
}

void Write(std::string_view text) {
    std::lock_guard lock(g_mutex);
    if (g_file == nullptr) {
        return;
    }

    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(g_file, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %.*s\n", time.wYear,
                 time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
                 GetCurrentThreadId(), static_cast<int>(text.size()), text.data());
    std::fflush(g_file);
}

}  // namespace fair_transmog::log
