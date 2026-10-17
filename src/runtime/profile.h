#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace fair_transmog::profile {

// This packed disk-file identity does not establish a validated runtime profile.
inline constexpr std::string_view kKnownPackedExecutableSha256 =
    "C2EBBBD2C49F216D484E31A5219BED419EB1E5E7D206D02CBA040A3AB79D90EA";

struct HostExecutableIdentity {
    HMODULE module = nullptr;
    std::filesystem::path disk_path;
    std::uint64_t disk_size = 0;
    std::string disk_sha256;
};

// A null module requests the actual host executable's path from Windows.
std::optional<std::filesystem::path> ModuleDiskPath(HMODULE module, std::string& error);

// Hashes a read-only disk snapshot; outputs change only on success.
bool HashDiskFile(const std::filesystem::path& path, std::string& sha256,
                  std::uint64_t& disk_size, std::string& error);

// Reads and hashes the host executable on disk, never its unpacked loaded image.
// Failure returns no identity; callers must keep all game features disabled.
std::optional<HostExecutableIdentity> InspectHostExecutable(std::string& error);

}  // namespace fair_transmog::profile
