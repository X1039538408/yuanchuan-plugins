#include "thumbnail_cache.h"

#include <gdiplus.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

void hash_text(std::uint64_t& hash, const std::wstring& text) {
    hash_bytes(hash, text.data(), text.size() * sizeof(wchar_t));
}

std::wstring hex_hash(std::uint64_t value) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result(16, L'0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[value & 0x0f];
        value >>= 4;
    }
    return result;
}

fs::path configured_cache_directory() {
    wchar_t value[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", value,
        static_cast<DWORD>(std::size(value)));
    if (length && length < std::size(value)) return fs::path(value, value + length);

    wchar_t local_app_data[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local_app_data))) {
        return {};
    }
    return fs::path(local_app_data) / L"VideoCopilot" / L"effects_cache";
}

fs::path normalized_source(const fs::path& source) {
    std::error_code error;
    const auto absolute = fs::absolute(source, error);
    return (error ? source : absolute).lexically_normal().make_preferred();
}

bool ensure_directory(const fs::path& directory) {
    if (directory.empty()) return false;
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return false;
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) return false;
    const DWORD created = GetFileAttributesW(directory.c_str());
    return created != INVALID_FILE_ATTRIBUTES &&
        (created & FILE_ATTRIBUTE_DIRECTORY) && !(created & FILE_ATTRIBUTE_REPARSE_POINT);
}

void ensure_cache_header(const fs::path& directory) {
    if (directory.empty()) return;
    const fs::path header = directory / L"effects_cache.header";
    HANDLE file = CreateFileW(header.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    constexpr char contents[] = "XFXThumbnailCache/1\n";
    DWORD written = 0;
    WriteFile(file, contents, static_cast<DWORD>(sizeof(contents) - 1), &written, nullptr);
    CloseHandle(file);
}

bool png_encoder(CLSID& encoder) {
    return CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &encoder) == S_OK;
}

std::unique_ptr<Gdiplus::Bitmap> load_bitmap(const fs::path& path) {
    auto image = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok || !image->GetWidth() || !image->GetHeight()) return {};
    return image;
}

bool save_bitmap(Gdiplus::Bitmap& image, const fs::path& target) {
    CLSID encoder{};
    if (!png_encoder(encoder) || !ensure_directory(target.parent_path())) return false;
    const fs::path temporary = target.wstring() + L".tmp-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    if (image.Save(temporary.c_str(), &encoder) != Gdiplus::Ok) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

namespace xfx {

std::filesystem::path thumbnail_cache_directory() {
    return configured_cache_directory();
}

std::filesystem::path thumbnail_cache_file(const std::filesystem::path& source) {
    const auto directory = configured_cache_directory();
    if (directory.empty()) return {};
    const auto normalized = normalized_source(source);
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, normalized.wstring());
    hash_text(hash, L"\0");
    std::error_code error;
    const auto size = fs::is_regular_file(normalized, error) ? fs::file_size(normalized, error) : 0;
    hash_bytes(hash, &size, sizeof(size));
    const auto modified = fs::last_write_time(normalized, error).time_since_epoch().count();
    hash_bytes(hash, &modified, sizeof(modified));
    return directory / (hex_hash(hash) + L".png");
}

ThumbnailCacheClearResult clear_thumbnail_cache() {
    ThumbnailCacheClearResult result;
    const auto directory = configured_cache_directory();
    if (directory.empty()) {
        result.failed = 1;
        result.first_error = ERROR_PATH_NOT_FOUND;
        return result;
    }
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return result;
        result.failed = 1;
        result.first_error = error;
        return result;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        result.failed = 1;
        result.first_error = ERROR_ACCESS_DENIED;
        return result;
    }

    std::error_code iteration_error;
    for (fs::directory_iterator entry(directory, fs::directory_options::skip_permission_denied, iteration_error);
         entry != fs::directory_iterator(); entry.increment(iteration_error)) {
        if (iteration_error) break;
        const auto path = entry->path();
        const DWORD entry_attributes = GetFileAttributesW(path.c_str());
        if (entry_attributes == INVALID_FILE_ATTRIBUTES) {
            ++result.failed;
            if (!result.first_error) result.first_error = GetLastError();
            continue;
        }
        if (entry_attributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (entry_attributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension != L".png" && extension != L".header") continue;
        if (DeleteFileW(path.c_str())) {
            ++result.deleted;
        } else {
            ++result.failed;
            if (!result.first_error) result.first_error = GetLastError();
        }
    }
    if (iteration_error) {
        ++result.failed;
        if (!result.first_error) result.first_error = static_cast<DWORD>(iteration_error.value());
    }
    return result;
}

bool draw_cached_thumbnail(HDC dc, const RECT& bounds, const std::filesystem::path& source) {
    if (!dc || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return false;
    const auto cache = thumbnail_cache_file(source);
    if (cache.empty()) return false;
    if (ensure_directory(cache.parent_path())) ensure_cache_header(cache.parent_path());

    std::unique_ptr<Gdiplus::Bitmap> image = load_bitmap(cache);
    if (!image) {
        const auto original = load_bitmap(source);
        if (!original) return false;
        const auto width = static_cast<UINT>((std::min)(original->GetWidth(), 512u));
        const auto height = (std::max)(1u, static_cast<UINT>(
            (static_cast<std::uint64_t>(original->GetHeight()) * width) / original->GetWidth()));
        auto thumbnail = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
        if (thumbnail->GetLastStatus() != Gdiplus::Ok) return false;
        Gdiplus::Graphics render(thumbnail.get());
        render.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        render.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (render.DrawImage(original.get(), Gdiplus::Rect(0, 0, static_cast<INT>(width), static_cast<INT>(height)),
            0, 0, static_cast<INT>(original->GetWidth()), static_cast<INT>(original->GetHeight()),
            Gdiplus::UnitPixel) != Gdiplus::Ok) return false;
        save_bitmap(*thumbnail, cache);
        image = std::move(thumbnail);
    }

    const double scale = (std::min)(
        static_cast<double>(bounds.right - bounds.left) / image->GetWidth(),
        static_cast<double>(bounds.bottom - bounds.top) / image->GetHeight());
    const int width = (std::max)(1, static_cast<int>(image->GetWidth() * scale));
    const int height = (std::max)(1, static_cast<int>(image->GetHeight() * scale));
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    return graphics.DrawImage(image.get(), bounds.left + (bounds.right - bounds.left - width) / 2,
        bounds.top + (bounds.bottom - bounds.top - height) / 2, width, height) == Gdiplus::Ok;
}

} // namespace xfx
