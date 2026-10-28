#pragma once

#include <cstddef>
#include <filesystem>
#include <windows.h>

namespace xfx {

struct ThumbnailCacheClearResult {
    std::size_t deleted = 0;
    std::size_t failed = 0;
    DWORD first_error = ERROR_SUCCESS;
};

std::filesystem::path thumbnail_cache_directory();
std::filesystem::path thumbnail_cache_file(const std::filesystem::path& source);
ThumbnailCacheClearResult clear_thumbnail_cache();
bool draw_cached_thumbnail(HDC dc, const RECT& bounds, const std::filesystem::path& source);

} // namespace xfx
