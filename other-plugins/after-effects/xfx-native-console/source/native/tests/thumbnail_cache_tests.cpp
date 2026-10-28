#include "../src/thumbnail_cache.h"
#include <windows.h>
#include <gdiplus.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void write_bytes(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

int wmain() {
    const fs::path root = fs::temp_directory_path() /
        (L"xfx-thumbnail-cache-tests-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);
    fs::create_directories(root);
    SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", (root / L"cache").c_str());

    Gdiplus::GdiplusStartupInput startup_input;
    ULONG_PTR token = 0;
    require(Gdiplus::GdiplusStartup(&token, &startup_input, nullptr) == Gdiplus::Ok,
        "GDI+ startup failed");

    try {
        const fs::path source = root / L"source.png";
        write_bytes(source, "source-v1");
        const auto first = xfx::thumbnail_cache_file(source);
        require(first.parent_path() == root / L"cache", "cache file escaped configured cache root");
        require(first.extension() == L".png", "cache file must use PNG extension");
        require(first == xfx::thumbnail_cache_file(source), "cache key was not stable");
        write_bytes(source, "source-v2-with-a-different-length");
        require(first != xfx::thumbnail_cache_file(source),
            "cache key did not change after source metadata changed");

        fs::create_directories(first.parent_path());
        write_bytes(first, "thumbnail");
        write_bytes(first.parent_path() / L"stale.header", "header");
        write_bytes(first.parent_path() / L"keep.txt", "keep");
        fs::create_directories(first.parent_path() / L"nested");
        write_bytes(first.parent_path() / L"nested" / L"nested.png", "nested");
        const auto cleared = xfx::clear_thumbnail_cache();
        require(cleared.deleted >= 2, "clear did not delete PNG and header cache entries");
        require(!fs::exists(first) && !fs::exists(first.parent_path() / L"stale.header"),
            "clear left cache entries behind");
        require(fs::exists(first.parent_path() / L"keep.txt"),
            "clear removed an unrelated cache file");
        require(fs::exists(first.parent_path() / L"nested" / L"nested.png"),
            "clear unexpectedly recursed into nested directories");

        const fs::path image = root / L"image.png";
        {
            Gdiplus::Bitmap bitmap(12, 8, PixelFormat32bppARGB);
            bitmap.SetPixel(0, 0, Gdiplus::Color(255, 12, 34, 56));
            CLSID encoder{};
            require(CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &encoder) == S_OK,
                "PNG encoder lookup failed");
            require(bitmap.Save(image.c_str(), &encoder) == Gdiplus::Ok,
                "source image save failed");
        }
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP surface = CreateCompatibleBitmap(screen, 80, 60);
        HGDIOBJ previous = SelectObject(memory, surface);
        RECT bounds{0, 0, 80, 60};
        require(xfx::draw_cached_thumbnail(memory, bounds, image),
            "cached thumbnail draw failed");
        SelectObject(memory, previous);
        DeleteObject(surface);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        require(fs::exists(xfx::thumbnail_cache_file(image)),
            "drawing a new thumbnail did not create the cache file");
        require(fs::exists(xfx::thumbnail_cache_directory() / L"effects_cache.header"),
            "drawing a new thumbnail did not create the cache header");

        Gdiplus::GdiplusShutdown(token);
        token = 0;
        fs::remove_all(root);
        SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", nullptr);
        std::cout << "thumbnail cache tests passed\n";
        return 0;
    } catch (...) {
        if (token) Gdiplus::GdiplusShutdown(token);
        SetEnvironmentVariableW(L"XFX_THUMBNAIL_CACHE_DIR", nullptr);
        fs::remove_all(root);
        throw;
    }
}
