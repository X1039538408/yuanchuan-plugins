#include "console_support.h"
#include <gdiplus.h>
#include <algorithm>
#include <charconv>
#include <cwctype>
#include <cstring>
#include <limits>
#include <new>
#include <memory>
#include <string_view>

namespace {
std::size_t dib_pixel_bytes(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > static_cast<std::uint32_t>((std::numeric_limits<LONG>::max)()) ||
        height > static_cast<std::uint32_t>((std::numeric_limits<LONG>::max)())) return 0;
    const auto bytes = static_cast<std::uint64_t>(width) * height * 4;
    if (bytes > (std::numeric_limits<DWORD>::max)() ||
        bytes > (std::numeric_limits<std::size_t>::max)() - sizeof(BITMAPV5HEADER)) return 0;
    return static_cast<std::size_t>(bytes);
}

struct CloseFileHandle {
    void operator()(void* handle) const { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
using FileHandle = std::unique_ptr<void, CloseFileHandle>;

bool same_windows_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    const auto a = left.wstring(), b = right.wstring();
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
        static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::filesystem::path normalized_path(std::filesystem::path path) {
    path = path.lexically_normal().make_preferred();
    while (path != path.root_path() && path.filename().empty()) path = path.parent_path();
    return path;
}

std::filesystem::path comparable_windows_path(const std::filesystem::path& path) {
    auto text = path.wstring();
    if (text.rfind(L"\\\\?\\UNC\\", 0) == 0) text = L"\\\\" + text.substr(8);
    else if (text.rfind(L"\\\\?\\", 0) == 0) text.erase(0, 4);
    return normalized_path(std::filesystem::path(text));
}

std::filesystem::path final_handle_path(HANDLE handle) {
    const DWORD needed = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!needed) return {};
    std::wstring path(needed, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle, path.data(), needed, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= needed) return {};
    path.resize(written);
    return normalized_path(std::filesystem::path(path));
}

bool has_traversal(const std::filesystem::path& path) {
    for (const auto& component : path) if (component == L"." || component == L"..") return true;
    return false;
}

DWORD directory_reparse_error(const std::filesystem::path& directory) {
    wchar_t volume_path[32768] = {};
    if (!GetVolumePathNameW(directory.c_str(), volume_path, 32768)) return GetLastError();
    const auto volume_root = comparable_windows_path(std::filesystem::path(volume_path));
    for (auto part = directory; !part.empty();) {
        auto comparable = comparable_windows_path(part);
        // std::filesystem may yield \\?\C: as the parent of an extended
        // drive root. Win32 requires the final slash for that root query.
        if (comparable.has_root_name() && !comparable.has_root_directory() && comparable.relative_path().empty()) {
            part += L"\\";
            comparable = comparable_windows_path(part);
        }
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return GetLastError();
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_ACCESS_DENIED;
        if (same_windows_path(comparable, volume_root)) break;
        const auto parent = part.parent_path();
        if (parent == part) break;
        part = parent;
    }
    return ERROR_SUCCESS;
}
}

namespace xfx {
std::wstring snapshot_png_filename(const std::wstring& composition_name) {
    std::wstring stem;
    bool meaningful = false;
    for (const wchar_t ch : composition_name) {
        const bool invalid = ch < 0x20 || ch == 0x7f ||
            std::wstring_view(L"<>:\"/\\|?*").find(ch) != std::wstring_view::npos;
        stem += invalid ? L'_' : ch;
        if (!invalid && ch != L'.' && ch != L' ') meaningful = true;
    }
    const auto trim_trailing = [](std::wstring& value) {
        while (!value.empty() && (value.back() == L'.' || value.back() == L' ')) value.pop_back();
    };
    trim_trailing(stem);
    if (!meaningful || stem.empty()) stem = L"合成快照";

    std::wstring extension = L".png";
    if (stem.size() >= 4) {
        const auto suffix = stem.substr(stem.size() - 4);
        if (CompareStringOrdinal(suffix.c_str(), 4, L".png", 4, TRUE) == CSTR_EQUAL) {
            extension = suffix;
            stem.resize(stem.size() - 4);
        }
    }
    if (stem.empty()) stem = L"合成快照";
    const auto reserved_device = [&](const std::wstring& value) {
        auto basename = value.substr(0, value.find(L'.'));
        trim_trailing(basename);
        for (auto& ch : basename) if (ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
        if (basename == L"CON" || basename == L"PRN" || basename == L"AUX" || basename == L"NUL") return true;
        return basename.size() == 4 && (basename.compare(0, 3, L"COM") == 0 ||
            basename.compare(0, 3, L"LPT") == 0) && ((basename[3] >= L'1' && basename[3] <= L'9') ||
            basename[3] == L'¹' || basename[3] == L'²' || basename[3] == L'³');
    };
    if (reserved_device(stem)) stem.insert(stem.begin(), L'_');
    constexpr std::size_t max_stem_units = 255 - 4;
    if (stem.size() > max_stem_units) {
        auto length = max_stem_units;
        if (stem[length - 1] >= 0xd800 && stem[length - 1] <= 0xdbff &&
            stem[length] >= 0xdc00 && stem[length] <= 0xdfff) --length;
        stem.resize(length);
        trim_trailing(stem);
    }
    if (stem.empty()) stem = L"合成快照";
    // Truncation can expose a device name previously followed by spaces.
    if (reserved_device(stem)) stem.insert(stem.begin(), L'_');
    return stem + extension;
}

std::vector<CaptureDeleteResult> delete_captures(const std::filesystem::path& directory,
                                              const std::vector<std::filesystem::path>& selected) {
    std::vector<CaptureDeleteResult> result;
    for (const auto& path : selected) result.push_back({path, false, ERROR_SUCCESS});
    if (result.empty()) return result;
    const auto fail_batch = [&](DWORD error) {
        for (auto& item : result) if (item.error == ERROR_SUCCESS) item.error = error;
    };
    std::error_code path_error;
    const auto root = normalized_path(std::filesystem::absolute(directory, path_error));
    if (directory.empty() || path_error) { fail_batch(ERROR_INVALID_NAME); return result; }
    const DWORD reparse_error = directory_reparse_error(root);
    if (reparse_error) { fail_batch(reparse_error); return result; }
    // Omitting FILE_SHARE_DELETE pins the directory while files are validated.
    HANDLE raw_directory = CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw_directory == INVALID_HANDLE_VALUE) { fail_batch(GetLastError()); return result; }
    FileHandle directory_handle(raw_directory);
    BY_HANDLE_FILE_INFORMATION directory_info{};
    if (!GetFileInformationByHandle(raw_directory, &directory_info)) { fail_batch(GetLastError()); return result; }
    if (!(directory_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        directory_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        fail_batch(ERROR_ACCESS_DENIED);
        return result;
    }
    const auto final_root = final_handle_path(raw_directory);
    if (final_root.empty()) { fail_batch(ERROR_ACCESS_DENIED); return result; }
    // An ancestor could have become a junction after the earlier attribute
    // checks. Never accept a directory handle that resolves outside the given root.
    if (!same_windows_path(comparable_windows_path(final_root), comparable_windows_path(root))) {
        fail_batch(ERROR_ACCESS_DENIED);
        return result;
    }
    std::vector<FileHandle> handles(selected.size());
    std::vector<std::filesystem::path> validated_paths;
    bool invalid = false;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto& requested = selected[i];
        const auto reject = [&](DWORD error) { result[i].error = error; invalid = true; };
        if (requested.empty() || has_traversal(requested) ||
            (!requested.is_absolute() && requested.has_parent_path())) {
            reject(ERROR_INVALID_NAME);
            continue;
        }
        const auto path = normalized_path(requested.is_absolute() ? requested : root / requested);
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        const auto filename = path.filename().wstring();
        if (!same_windows_path(path.parent_path(), root) || extension != L".png" ||
            filename.find(L':') != std::wstring::npos) {
            reject(ERROR_ACCESS_DENIED);
            continue;
        }
        bool duplicate = false;
        for (const auto& prior : validated_paths) if (same_windows_path(prior, path)) { duplicate = true; break; }
        if (duplicate) { reject(ERROR_INVALID_PARAMETER); continue; }
        // Open the final root path, not a user-supplied traversal or reparse target.
        const auto pinned_path = final_root / path.filename();
        HANDLE raw_file = CreateFileW(pinned_path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        DWORD operation_error = ERROR_SUCCESS;
        if (raw_file == INVALID_HANDLE_VALUE) {
            operation_error = GetLastError();
            const DWORD attributes = GetFileAttributesW(pinned_path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) { reject(GetLastError()); continue; }
            if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
                reject(ERROR_ACCESS_DENIED);
                continue;
            }
            // A structurally valid but locked/permission-denied PNG is an
            // individual I/O failure, not an invalid selection for the whole batch.
            raw_file = CreateFileW(pinned_path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (raw_file == INVALID_HANDLE_VALUE) {
                result[i].error = operation_error;
                validated_paths.push_back(path);
                continue;
            }
        }
        handles[i] = FileHandle(raw_file);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(raw_file, &info)) { reject(GetLastError()); continue; }
        if (GetFileType(raw_file) != FILE_TYPE_DISK ||
            info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) {
            reject(ERROR_ACCESS_DENIED);
            continue;
        }
        const auto final_file = final_handle_path(raw_file);
        if (final_file.empty() || !same_windows_path(final_file.parent_path(), final_root)) {
            reject(ERROR_ACCESS_DENIED);
            continue;
        }
        result[i].error = operation_error;
        validated_paths.push_back(path);
    }
    if (invalid) { fail_batch(ERROR_OPERATION_ABORTED); return result; }
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (result[i].error != ERROR_SUCCESS) { handles[i].reset(); continue; }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (SetFileInformationByHandle(handles[i].get(), FileDispositionInfo, &disposition, sizeof(disposition))) {
            result[i].deleted = true;
        } else {
            result[i].error = GetLastError();
        }
        // This closes the validated handle and completes deletion of that entry.
        handles[i].reset();
    }
    return result;
}

std::vector<std::uint8_t> pack_bgra_dib(std::uint32_t width, std::uint32_t height,
                                     std::span<const std::uint8_t> pixels) {
    const auto bytes = dib_pixel_bytes(width, height);
    if (!bytes || pixels.size() != bytes || !pixels.data()) return {};
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(width);
    header.bV5Height = -static_cast<LONG>(height);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(bytes);
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;
    try {
        std::vector<std::uint8_t> dib(sizeof(header) + bytes);
        std::memcpy(dib.data(), &header, sizeof(header));
        std::memcpy(dib.data() + sizeof(header), pixels.data(), bytes);
        return dib;
    } catch (const std::bad_alloc&) { return {}; }
}

std::vector<std::uint8_t> load_capture_dib(const std::filesystem::path& file) {
    Gdiplus::Bitmap image(file.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok) return {};
    const auto width = image.GetWidth(), height = image.GetHeight();
    const auto bytes = dib_pixel_bytes(width, height);
    if (!bytes) return {};
    try {
        std::vector<std::uint8_t> pixels(bytes);
        Gdiplus::Rect bounds(0, 0, static_cast<INT>(width), static_cast<INT>(height));
        Gdiplus::BitmapData locked{};
        if (image.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &locked) != Gdiplus::Ok) return {};
        const auto row_bytes = static_cast<std::size_t>(width) * 4;
        const auto stride = static_cast<std::int64_t>(locked.Stride);
        const auto stride_bytes = stride < 0 ? -stride : stride;
        const bool valid = locked.Scan0 && static_cast<std::uint64_t>(stride_bytes) >= row_bytes;
        if (valid) {
            // Scan0 addresses the first logical row; a negative stride walks backwards.
            for (std::uint32_t row = 0; row < height; ++row) {
                const auto* source = static_cast<const std::uint8_t*>(locked.Scan0) +
                    static_cast<std::ptrdiff_t>(row) * locked.Stride;
                std::memcpy(pixels.data() + static_cast<std::size_t>(row) * row_bytes, source, row_bytes);
            }
        }
        const auto unlocked = image.UnlockBits(&locked);
        if (!valid || unlocked != Gdiplus::Ok) return {};
        return pack_bgra_dib(width, height, pixels);
    } catch (const std::bad_alloc&) { return {}; }
}

bool copy_capture_to_clipboard(HWND owner, const std::filesystem::path& file) {
    if (!owner || !IsWindow(owner)) return false;
    const auto dib = load_capture_dib(file);
    if (dib.empty()) return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, dib.size());
    if (!memory) return false;
    void* data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); return false; }
    std::memcpy(data, dib.data(), dib.size());
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) { GlobalFree(memory); return false; }
    const bool success = EmptyClipboard() && SetClipboardData(CF_DIBV5, memory);
    CloseClipboard();
    // On success the clipboard owns memory; otherwise ownership remains here.
    if (!success) GlobalFree(memory);
    return success;
}

ScriptReply parse_script_reply(const std::string& text) {
    ScriptReply reply;
    const auto first = text.find('\t');
    if (first == std::string::npos) { reply.message = text; return reply; }
    const auto status = text.substr(0, first);
    if (status == "ERROR") { reply.message = text.substr(first + 1); return reply; }
    if (status != "OK" && status != "PARTIAL") { reply.message = text; return reply; }
    const auto second = text.find('\t', first + 1);
    const auto number = text.substr(first + 1, second == std::string::npos ? second : second - first - 1);
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), reply.count);
    if (number.empty() || parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) {
        reply.message = "Invalid script result";
        return reply;
    }
    reply.succeeded = status == "OK";
    reply.partial = status == "PARTIAL";
    if (second != std::string::npos) reply.message = text.substr(second + 1);
    return reply;
}

bool should_close_console(bool auto_close, bool fixed, unsigned int modal_depth) {
    (void)auto_close;
    (void)fixed;
    return modal_depth == 0;
}

std::vector<std::filesystem::path> scan_captures(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        auto extension = it->path().extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".png") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        std::error_code ea, eb;
        const auto ta = std::filesystem::last_write_time(a, ea);
        const auto tb = std::filesystem::last_write_time(b, eb);
        if (!ea && !eb && ta != tb) return ta > tb;
        return a.filename() > b.filename();
    });
    return files;
}

bool export_capture(const std::filesystem::path& source, const std::filesystem::path& target, bool jpeg) {
    Gdiplus::Bitmap image(source.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok) return false;
    CLSID encoder{};
    if (CLSIDFromString(jpeg ? L"{557cf401-1a04-11d3-9a73-0000f81ef32e}" :
        L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &encoder) != S_OK) return false;
    if (!jpeg) return image.Save(target.c_str(), &encoder) == Gdiplus::Ok;
    // JPEG has no alpha. Explicitly composite onto white instead of losing it to black.
    Gdiplus::Bitmap flattened(image.GetWidth(), image.GetHeight(), PixelFormat24bppRGB);
    {
        Gdiplus::Graphics graphics(&flattened);
        graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
        graphics.DrawImage(&image, 0, 0, image.GetWidth(), image.GetHeight());
    }
    Gdiplus::EncoderParameters parameters{};
    ULONG quality = 95;
    parameters.Count = 1;
    parameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
    parameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    parameters.Parameter[0].NumberOfValues = 1;
    parameters.Parameter[0].Value = &quality;
    return flattened.Save(target.c_str(), &encoder, &parameters) == Gdiplus::Ok;
}

bool sample_image_pixel(const std::filesystem::path& file, unsigned int x, unsigned int y, unsigned int& argb) {
    Gdiplus::Bitmap image(file.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || x >= image.GetWidth() || y >= image.GetHeight()) return false;
    Gdiplus::Color color;
    if (image.GetPixel(x, y, &color) != Gdiplus::Ok) return false;
    argb = color.GetValue();
    return true;
}

bool draw_capture(HDC dc, const RECT& bounds, const std::filesystem::path& file) {
    Gdiplus::Bitmap image(file.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || !image.GetWidth() || !image.GetHeight()) return false;
    const double scale = (std::min)(static_cast<double>(bounds.right - bounds.left) / image.GetWidth(),
        static_cast<double>(bounds.bottom - bounds.top) / image.GetHeight());
    const int width = static_cast<int>(image.GetWidth() * scale);
    const int height = static_cast<int>(image.GetHeight() * scale);
    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    return graphics.DrawImage(&image, bounds.left + (bounds.right - bounds.left - width) / 2,
        bounds.top + (bounds.bottom - bounds.top - height) / 2, width, height) == Gdiplus::Ok;
}
}
