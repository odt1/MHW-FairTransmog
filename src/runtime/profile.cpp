#include "runtime/profile.h"

#include <bcrypt.h>

#include <array>
#include <format>
#include <utility>

namespace fair_transmog::profile {
namespace {

struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;

    FileHandle() = default;
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    ~FileHandle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};

struct HashHandles {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    HashHandles() = default;
    HashHandles(const HashHandles&) = delete;
    HashHandles& operator=(const HashHandles&) = delete;

    ~HashHandles() {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    }
};

std::string Win32Error(std::string_view operation, DWORD code) {
    return std::format("{} failed (Win32 {}).", operation, code);
}

std::string HashError(std::string_view operation, NTSTATUS status) {
    return std::format("{} failed (NTSTATUS 0x{:08X}).", operation,
                       static_cast<std::uint32_t>(status));
}

}  // namespace

bool HashDiskFile(const std::filesystem::path& path, std::string& sha256,
                  std::uint64_t& disk_size, std::string& error) {
    error.clear();
    FileHandle file;
    // Deny ordinary writes and replacement while this disk-file snapshot is read.
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        error = Win32Error("CreateFileW(host executable)", GetLastError());
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) {
        error = Win32Error("GetFileSizeEx(host executable)", GetLastError());
        return false;
    }
    if (size.QuadPart < 0) {
        error = "Host executable reported an invalid negative disk size.";
        return false;
    }
    const auto expected_size = static_cast<std::uint64_t>(size.QuadPart);

    HashHandles handles;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM,
                                                  nullptr, 0);
    if (status < 0) {
        error = HashError("BCryptOpenAlgorithmProvider(SHA-256)", status);
        return false;
    }
    // Windows 7 and later allocate the hash object when both object arguments are zero.
    status = BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0);
    if (status < 0) {
        error = HashError("BCryptCreateHash(SHA-256)", status);
        return false;
    }

    std::array<UCHAR, 64 * 1024> buffer{};
    std::uint64_t total_read = 0;
    for (;;) {
        DWORD bytes_read = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
                      nullptr)) {
            error = Win32Error("ReadFile(host executable)", GetLastError());
            return false;
        }
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read > expected_size - total_read) {
            error = "Host executable disk size changed during hashing.";
            return false;
        }
        status = BCryptHashData(handles.hash, buffer.data(), bytes_read, 0);
        if (status < 0) {
            error = HashError("BCryptHashData(SHA-256)", status);
            return false;
        }
        total_read += bytes_read;
    }
    if (total_read != expected_size) {
        error = "Host executable read ended before the expected disk size; hash rejected.";
        return false;
    }

    std::array<UCHAR, 32> digest{};
    status = BCryptFinishHash(handles.hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) {
        error = HashError("BCryptFinishHash(SHA-256)", status);
        return false;
    }

    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded(digest.size() * 2, '\0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        encoded[i * 2] = hex[digest[i] >> 4];
        encoded[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    sha256 = std::move(encoded);
    disk_size = expected_size;
    return true;
}

std::optional<std::filesystem::path> ModuleDiskPath(HMODULE module, std::string& error) {
    error.clear();
    constexpr DWORD capacity = 32768;
    std::wstring buffer(capacity, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), capacity);
    if (length == 0) {
        error = Win32Error("GetModuleFileNameW", GetLastError());
        return std::nullopt;
    }
    if (length >= capacity) {
        error = "GetModuleFileNameW returned a truncated module path; identity rejected.";
        return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path(std::move(buffer));
}

std::optional<HostExecutableIdentity> InspectHostExecutable(std::string& error) {
    error.clear();
    HostExecutableIdentity identity;
    identity.module = GetModuleHandleW(nullptr);
    if (identity.module == nullptr) {
        error = Win32Error("GetModuleHandleW(host executable)", GetLastError());
        return std::nullopt;
    }

    auto path = ModuleDiskPath(nullptr, error);
    if (!path) {
        return std::nullopt;
    }
    identity.disk_path = std::move(*path);
    if (!HashDiskFile(identity.disk_path, identity.disk_sha256, identity.disk_size, error)) {
        return std::nullopt;
    }
    return identity;
}

}  // namespace fair_transmog::profile
