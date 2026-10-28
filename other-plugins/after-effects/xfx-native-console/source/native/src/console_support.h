#pragma once
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <windows.h>

namespace xfx {
struct ScriptReply {
    bool succeeded = false;
    bool partial = false;
    unsigned int count = 0;
    std::string message;
};
ScriptReply parse_script_reply(const std::string& text);
bool should_close_console(bool auto_close, bool fixed, unsigned int modal_depth);
// Safe Windows PNG leaf filename derived from the current composition name.
std::wstring snapshot_png_filename(const std::wstring& composition_name);
std::vector<std::filesystem::path> scan_captures(const std::filesystem::path& directory);
bool export_capture(const std::filesystem::path& source, const std::filesystem::path& target, bool jpeg);
bool sample_image_pixel(const std::filesystem::path& file, unsigned int x, unsigned int y, unsigned int& argb);
bool draw_capture(HDC dc, const RECT& bounds, const std::filesystem::path& file);
// Pure CF_DIBV5 payload builder. Input is tightly packed, top-down BGRA bytes.
// Invalid dimensions/buffer lengths return an empty vector.
std::vector<std::uint8_t> pack_bgra_dib(std::uint32_t width, std::uint32_t height,
                                     std::span<const std::uint8_t> pixels);
// GDI+ must already be initialized by the caller. No clipboard is touched.
std::vector<std::uint8_t> load_capture_dib(const std::filesystem::path& file);
bool copy_capture_to_clipboard(HWND owner, const std::filesystem::path& file);
struct CaptureDeleteResult {
    std::filesystem::path path;
    bool deleted = false;
    DWORD error = ERROR_SUCCESS;
};
// Prevalidate the whole batch before deleting anything. Any invalid selection
// aborts the batch; otherwise individual filesystem failures are reported.
// Only existing direct-child PNG files are accepted. Reparse points are rejected.
std::vector<CaptureDeleteResult> delete_captures(const std::filesystem::path& directory,
                                              const std::vector<std::filesystem::path>& selected);
}
