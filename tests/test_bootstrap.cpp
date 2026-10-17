#include <windows.h>

#include "runtime/profile.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace profile = fair_transmog::profile;
using Clock = std::chrono::steady_clock;

constexpr auto kBootstrapTimeout = std::chrono::seconds(10);
constexpr DWORD kChildTimeoutMs = 20000;
constexpr std::string_view kTerminal =
    "Fail closed: nothing installed; no game calls, hooks, or game-memory writes.";

// Intentionally never passed to FreeLibrary: replacement/removal requires process exit.
HMODULE g_plugin = nullptr;

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto root = std::filesystem::temp_directory_path();
        const auto prefix = L"fair-transmog-bootstrap-" + std::to_wstring(GetCurrentProcessId()) +
                            L"-" + std::to_wstring(GetTickCount64()) + L"-";
        for (unsigned attempt = 0; attempt != 128; ++attempt) {
            auto candidate = root / (prefix + std::to_wstring(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path = std::move(candidate);
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::system_error(error, "create bootstrap staging directory");
            }
        }
        throw std::runtime_error("Could not allocate a unique bootstrap staging directory.");
    }

    ~TemporaryDirectory() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::optional<std::string> ReadLog(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    if (size > 64 * 1024) {
        throw std::runtime_error("Bootstrap log exceeds the expected 64 KiB bound.");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool VerifyLog(const std::string& text, const std::filesystem::path& plugin_path,
               const profile::HostExecutableIdentity& host) {
    std::vector<std::string_view> messages;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto newline = text.find('\n', begin);
        if (newline == std::string::npos) {
            return false;  // A partial final line is not bootstrap completion.
        }
        auto line = std::string_view(text).substr(begin, newline - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto prefix_end = line.find("] ");
        if (prefix_end == std::string_view::npos) {
            return false;
        }
        messages.push_back(line.substr(prefix_end + 2));
        begin = newline + 1;
    }

    // An exact allowlist rejects extra feature-enable/hook/write messages, duplicate runs,
    // exception/failure paths, and a terminal marker without completed host inspection.
    const std::array<std::string, 6> expected{
        std::format("MHW FairTransmog {} starting.", FAIR_TRANSMOG_VERSION),
        std::format("Plugin module: path='{}', base=0x{:016X}.", Utf8(plugin_path),
                    reinterpret_cast<std::uintptr_t>(g_plugin)),
        std::format("Host module: path='{}', base=0x{:016X}, disk bytes={}.", Utf8(host.disk_path),
                    reinterpret_cast<std::uintptr_t>(host.module), host.disk_size),
        "Host executable SHA-256 (disk file, not unpacked loaded memory): " + host.disk_sha256 + ".",
        "Unsupported game executable; this version supports SHA-256 " +
            std::string(profile::kKnownPackedExecutableSha256) + " only.",
        std::string(kTerminal),
    };
    if (messages.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i != expected.size(); ++i) {
        if (messages[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

int RunChild(const std::filesystem::path& plugin_path) {
    auto log_path = plugin_path;
    log_path.replace_extension(L".log");
    if (!plugin_path.is_absolute() || std::filesystem::exists(log_path)) {
        std::fprintf(stderr, "Child requires an absolute DLL path and no pre-existing log.\n");
        return 2;
    }

    std::string error;
    const auto host = profile::InspectHostExecutable(error);
    if (!host || host->disk_sha256 == profile::kKnownPackedExecutableSha256) {
        std::fprintf(stderr, "Could not establish an unsupported test-host identity: %s\n",
                     error.c_str());
        return 3;
    }

    g_plugin = LoadLibraryW(plugin_path.c_str());
    if (g_plugin == nullptr) {
        std::fprintf(stderr, "LoadLibraryW failed (Win32 %lu).\n", GetLastError());
        return 4;
    }

    const auto deadline = Clock::now() + kBootstrapTimeout;
    while (Clock::now() < deadline) {
        const auto text = ReadLog(log_path);
        if (text && text->find(std::string(kTerminal) + "\r\n") != std::string::npos) {
            if (!VerifyLog(*text, plugin_path, *host)) {
                std::fprintf(stderr, "Completed bootstrap log did not match fail-closed expectations.\n");
                return 5;
            }
            for (const auto& entry : std::filesystem::directory_iterator(plugin_path.parent_path())) {
                if (entry.path() != plugin_path && entry.path() != log_path) {
                    std::fprintf(stderr, "Bootstrap produced an unexpected staging-directory entry.\n");
                    return 6;
                }
            }
            return 0;
        }
        // Polling only; elapsed time or the mere creation of a log never establishes success.
        Sleep(10);
    }
    std::fprintf(stderr, "Bootstrap terminal marker was not observed within ten seconds.\n");
    return 7;
}

int RunParent(const std::filesystem::path& source_dll) {
    if (!source_dll.is_absolute() || !std::filesystem::is_regular_file(source_dll)) {
        std::fprintf(stderr, "Pass an absolute path to the already-built test DLL.\n");
        return 2;
    }

    TemporaryDirectory staging;
    const auto staged_dll = staging.path / L"MHW-FairTransmog.dll";
    auto log_path = staged_dll;
    log_path.replace_extension(L".log");
    if (!CopyFileW(source_dll.c_str(), staged_dll.c_str(), TRUE)) {
        std::fprintf(stderr, "Could not copy the DLL into staging (Win32 %lu).\n", GetLastError());
        return 3;
    }
    if (std::filesystem::exists(log_path)) {
        throw std::runtime_error("Unexpected pre-existing staging log.");
    }

    std::string error;
    const auto executable = profile::ModuleDiskPath(nullptr, error);
    if (!executable) {
        throw std::runtime_error(error);
    }
    // Windows filenames cannot contain a double quote, and both paths end in filenames.
    std::wstring command = L"\"" + executable->wstring() + L"\" --child \"" +
                           staged_dll.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable->c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        staging.path.c_str(), &startup, &process)) {
        std::fprintf(stderr, "Could not start bootstrap child (Win32 %lu).\n", GetLastError());
        return 4;
    }
    Handle child;
    child.value = process.hProcess;
    Handle thread;
    thread.value = process.hThread;

    const DWORD wait = WaitForSingleObject(child.value, kChildTimeoutMs);
    if (wait != WAIT_OBJECT_0) {
        // Dispose only of this test-owned child; no game process is launched or attached.
        const BOOL terminated = TerminateProcess(child.value, 8);
        const DWORD stopped = terminated ? WaitForSingleObject(child.value, 5000) : WAIT_FAILED;
        if (stopped != WAIT_OBJECT_0) {
            // Do not delete a DLL/log while its test process might still be using it.
            std::fprintf(stderr, "Could not stop child; leaving staging at %s\n",
                         Utf8(staging.path).c_str());
            staging.path.clear();
        }
        std::fprintf(stderr, "Bootstrap child did not exit within twenty seconds (wait=%lu).\n", wait);
        return 8;
    }

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(child.value, &exit_code) || exit_code != 0) {
        std::fprintf(stderr, "Bootstrap child failed (exit=%lu).\n", exit_code);
        if (const auto text = ReadLog(log_path)) {
            std::fprintf(stderr, "%s", text->c_str());
        }
        return 9;
    }

    // The child has exited: its DLL and CRT-owned log handle can now be removed safely.
    std::error_code cleanup_error;
    std::filesystem::remove_all(staging.path, cleanup_error);
    if (cleanup_error) {
        std::fprintf(stderr, "Could not remove test staging: %s\n", cleanup_error.message().c_str());
        return 10;
    }
    staging.path.clear();
    std::puts("bootstrap: unsupported host reached the logging-only fail-closed terminal marker");
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--child") {
            return RunChild(std::filesystem::path(argv[2]));
        }
        if (argc != 2) {
            std::fprintf(stderr, "Usage: test_bootstrap.exe <absolute-built-DLL-path>\n");
            return 1;
        }
        return RunParent(std::filesystem::path(argv[1]));
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "bootstrap test exception: %s\n", exception.what());
        return 1;
    }
}
