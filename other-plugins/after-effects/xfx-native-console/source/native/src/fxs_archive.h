#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xfx {

constexpr std::uint32_t kFxsMagic = 0x1A5E6376U;

struct FxsEntry {
    std::string path;
    std::uint32_t flags = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint64_t file_time_a = 0;
    std::uint64_t file_time_b = 0;
    std::vector<std::uint8_t> data;
};

struct FxsArchive {
    std::vector<std::string> required_paths;
    std::vector<FxsEntry> entries;
};

FxsArchive parse_fxs_bytes(const std::vector<std::uint8_t>& bytes);
FxsArchive parse_fxs_file(const std::filesystem::path& path);

} // namespace xfx
