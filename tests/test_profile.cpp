#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "runtime/profile.h"

namespace {

namespace profile = fair_transmog::profile;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

struct TempDirectory {
    std::filesystem::path path;

    TempDirectory() {
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        Require(length != 0 && length < buffer.size(), "GetTempPathW failed");
        buffer.resize(length);
        const auto prefix = L"FairTransmog-profile-" + std::to_wstring(GetCurrentProcessId()) +
                            L"-" + std::to_wstring(GetTickCount64()) + L"-";
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::path(buffer) / (prefix + std::to_wstring(attempt));
            if (CreateDirectoryW(candidate.c_str(), nullptr)) {
                path = std::move(candidate);
                return;
            }
            Require(GetLastError() == ERROR_ALREADY_EXISTS, "CreateDirectoryW failed");
        }
        throw std::runtime_error("Could not create a unique test directory");
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) {
            std::fprintf(stderr, "Temporary-file cleanup failed: %s\n", error.message().c_str());
        }
    }
};

void WriteFixture(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(output.is_open(), "Could not create test-owned fixture");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    Require(!output.fail(), "Could not finish writing test-owned fixture");
}

void CheckHash(const std::filesystem::path& path, std::string_view expected,
               std::uint64_t expected_size) {
    std::string digest = "stale digest";
    std::uint64_t size = 99;
    std::string error = "stale error";
    const bool success = profile::HashDiskFile(path, digest, size, error);
    Require(success, "HashDiskFile failed: " + error);
    Require(error.empty(), "Successful HashDiskFile did not clear error");
    Require(digest == expected, "HashDiskFile returned the wrong SHA-256");
    Require(size == expected_size, "HashDiskFile returned the wrong byte count");
}

void CheckHashFailure(const std::filesystem::path& path) {
    const std::string original_digest = "unchanged digest";
    constexpr std::uint64_t original_size = 0x123456789ULL;
    std::string digest = original_digest;
    std::uint64_t size = original_size;
    std::string error = "stale error";
    Require(!profile::HashDiskFile(path, digest, size, error),
            "HashDiskFile unexpectedly accepted an unreadable path");
    Require(!error.empty() && error != "stale error", "Hash failure did not report a fresh error");
    Require(digest == original_digest && size == original_size,
            "Hash failure changed caller outputs");
}

void TestKnownVectors() {
    TempDirectory temp;
    const auto empty = temp.path / L"empty.bin";
    const auto abc = temp.path / L"abc-Ω.bin";
    const auto many = temp.path / L"million-a.bin";
    WriteFixture(empty, "");
    WriteFixture(abc, "abc");
    // NIST's million-'a' vector crosses the production 64 KiB I/O boundary
    // repeatedly and ends with a partial chunk. No fixture uses game/save data.
    WriteFixture(many, std::string(1'000'000, 'a'));
    CheckHash(empty, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", 0);
    CheckHash(abc, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", 3);
    CheckHash(many, "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0",
              1'000'000);
}

void TestFileErrors() {
    TempDirectory temp;
    CheckHashFailure(temp.path / L"does-not-exist.bin");
    CheckHashFailure(temp.path);  // CreateFileW must not accept a directory as an executable.

    const auto locked_path = temp.path / L"sharing-denied.bin";
    WriteFixture(locked_path, "abc");
    {
        Handle locked;
        // Deterministic unreadability without changing ACLs or depending on elevation.
        locked.value = CreateFileW(locked_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked.value != INVALID_HANDLE_VALUE, "Could not lock test-owned fixture");
        CheckHashFailure(locked_path);
    }
    // A failed open must not poison subsequent hashing or retain a file handle.
    CheckHash(locked_path, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", 3);
    Require(DeleteFileW(locked_path.c_str()) != FALSE, "HashDiskFile retained a file handle");
}

std::filesystem::path ActualHostPath() {
    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    Require(QueryFullProcessImageNameW(GetCurrentProcess(), 0, buffer.data(), &length) != FALSE,
            "QueryFullProcessImageNameW failed");
    buffer.resize(length);
    return std::filesystem::path(std::move(buffer));
}

void TestModulePaths() {
    const auto actual = ActualHostPath();
    std::string error = "stale error";
    const auto implicit = profile::ModuleDiskPath(nullptr, error);
    Require(implicit.has_value(), "ModuleDiskPath(nullptr) failed: " + error);
    Require(error.empty(), "Successful ModuleDiskPath did not clear error");
    Require(std::filesystem::equivalent(*implicit, actual), "Null module did not resolve the test host");

    error = "stale error";
    const auto explicit_host = profile::ModuleDiskPath(GetModuleHandleW(nullptr), error);
    Require(explicit_host.has_value() && error.empty(), "Explicit host module resolution failed");
    Require(std::filesystem::equivalent(*explicit_host, actual), "Explicit host path is incorrect");

    error = "stale error";
    const auto invalid = profile::ModuleDiskPath(reinterpret_cast<HMODULE>(std::uintptr_t{1}), error);
    Require(!invalid.has_value(), "Invalid module handle unexpectedly resolved");
    Require(!error.empty() && error != "stale error", "Invalid module did not report a fresh error");
}

std::string HashInMemory(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "Could not read test host for independent hash");
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input),
                                          std::istreambuf_iterator<char>()};
    Require(!input.bad() && !bytes.empty(), "Could not finish reading test host");
    Require(bytes.size() <= std::numeric_limits<ULONG>::max(),
            "Test host is too large for one-shot CNG cross-check");

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    const NTSTATUS opened = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    Require(opened >= 0, "Independent BCryptOpenAlgorithmProvider failed");
    std::array<unsigned char, 32> digest{};
    // One-shot CNG with a separate whole-file read cross-checks the streaming API.
    const NTSTATUS hashed = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                                       static_cast<ULONG>(bytes.size()), digest.data(),
                                       static_cast<ULONG>(digest.size()));
    const NTSTATUS closed = BCryptCloseAlgorithmProvider(algorithm, 0);
    Require(hashed >= 0 && closed >= 0, "Independent one-shot CNG hash failed");
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        encoded.push_back(hex[byte >> 4]);
        encoded.push_back(hex[byte & 15]);
    }
    return encoded;
}

void TestHostIdentity() {
    const auto actual = ActualHostPath();
    std::string error = "stale error";
    const auto identity = profile::InspectHostExecutable(error);
    Require(identity.has_value(), "InspectHostExecutable failed: " + error);
    Require(error.empty(), "Successful InspectHostExecutable did not clear error");
    Require(identity->module == GetModuleHandleW(nullptr), "Inspected module is not this test host");
    Require(std::filesystem::equivalent(identity->disk_path, actual), "Inspected path is not this test host");
    Require(identity->disk_size == std::filesystem::file_size(actual), "Inspected file size is incorrect");
    Require(identity->disk_sha256 == HashInMemory(actual), "Inspected SHA-256 is not the test host's disk hash");
    Require(identity->disk_sha256 != profile::kKnownPackedExecutableSha256,
            "The synthetic test host unexpectedly matched the game's identity");
    // The API exposes identity only, not feature support. The known hash is not
    // fabricated as a fixture and must never be treated as an enable switch.
}

}  // namespace

int main() {
    int failures = 0;
    const auto run = [&failures](const char* name, auto test) {
        try {
            test();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& exception) {
            std::fprintf(stderr, "FAIL %s: %s\n", name, exception.what());
            ++failures;
        }
    };
    run("known SHA-256 vectors (empty, abc, multi-chunk)", TestKnownVectors);
    run("missing, directory, sharing-denied errors and recovery", TestFileErrors);
    run("actual host module path and invalid module", TestModulePaths);
    run("actual host disk identity with independent CNG cross-check", TestHostIdentity);
    return failures == 0 ? 0 : 1;
}
