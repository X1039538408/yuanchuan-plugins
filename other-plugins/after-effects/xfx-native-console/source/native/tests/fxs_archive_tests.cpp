#include "../src/fxs_archive.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string& value) {
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> archive_fixture(const std::vector<std::uint8_t>& compressed,
                                          const std::string& payload) {
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, xfx::kFxsMagic);
    append_u32(bytes, 1);
    append_string(bytes, "effects_settings/settings.ini");
    append_u32(bytes, 1);
    append_string(bytes, "effects_settings/settings.ini");
    append_u32(bytes, 1);
    append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
    append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    append_u64(bytes, 0);
    append_u64(bytes, 0);
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    return bytes;
}

std::vector<std::uint8_t> fixture() {
    return archive_fixture({
        0x78, 0xda, 0x73, 0x4b, 0x2c, 0xcb, 0x2f, 0xca, 0x2c, 0x49, 0xb5,
        0x75, 0xb5, 0x72, 0x74, 0x71, 0x72, 0x55, 0x70, 0xcb, 0xcc, 0xc9,
        0xe1, 0xe5, 0x02, 0x00, 0x56, 0xd1, 0x06, 0xc7,
    }, "Favorite=E:ADBE Fill\r\n");
}

int run() {
    const auto archive = xfx::parse_fxs_bytes(fixture());
    require(archive.required_paths.size() == 1, "required path count changed");
    require(archive.required_paths.front() == "effects_settings\\settings.ini", "required path changed");
    require(archive.entries.size() == 1, "entry count changed");
    require(archive.entries.front().path == "effects_settings\\settings.ini", "entry path changed");
    require(archive.entries.front().flags == 1, "entry flags changed");
    const std::string payload(archive.entries.front().data.begin(), archive.entries.front().data.end());
    require(payload == "Favorite=E:ADBE Fill\r\n", "zlib payload was not restored");

    auto truncated = fixture();
    truncated.resize(truncated.size() - 1);
    bool threw = false;
    try {
        (void)xfx::parse_fxs_bytes(truncated);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "truncated archive was accepted");

    auto traversal = fixture();
    const std::string needle = "effects_settings/settings.ini";
    const auto position = std::search(traversal.begin(), traversal.end(), needle.begin(), needle.end());
    require(position != traversal.end(), "fixture path was not found");
    const std::string replacement = "effects_settings/../evil.txt";
    std::copy(replacement.begin(), replacement.end(), position);
    threw = false;
    try {
        (void)xfx::parse_fxs_bytes(traversal);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "path traversal was accepted");

    const auto empty = xfx::parse_fxs_bytes(archive_fixture({
        0x78, 0xda, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01,
    }, ""));
    require(empty.entries.size() == 1 && empty.entries.front().data.empty(),
            "valid empty zlib entry was rejected");

    auto trailing_compressed = std::vector<std::uint8_t>{
        0x78, 0xda, 0x73, 0x4b, 0x2c, 0xcb, 0x2f, 0xca, 0x2c, 0x49, 0xb5,
        0x75, 0xb5, 0x72, 0x74, 0x71, 0x72, 0x55, 0x70, 0xcb, 0xcc, 0xc9,
        0xe1, 0xe5, 0x02, 0x00, 0x56, 0xd1, 0x06, 0xc7, 0x00,
    };
    threw = false;
    try {
        (void)xfx::parse_fxs_bytes(archive_fixture(trailing_compressed,
                                                    "Favorite=E:ADBE Fill\r\n"));
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "trailing compressed bytes were accepted");

    const auto file_path = std::filesystem::temp_directory_path() /
                           "xfx-fxs-archive-tests.fxs";
    const auto file_fixture = fixture();
    {
        std::ofstream output(file_path, std::ios::binary);
        require(static_cast<bool>(output), "could not create FXS fixture file");
        output.write(reinterpret_cast<const char*>(file_fixture.data()),
                     static_cast<std::streamsize>(file_fixture.size()));
        require(static_cast<bool>(output), "could not write FXS fixture file");
    }
    const auto file_archive = xfx::parse_fxs_file(file_path);
    std::filesystem::remove(file_path);
    require(file_archive.entries.size() == 1, "FXS file parsing failed");
    return 0;
}

} // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
