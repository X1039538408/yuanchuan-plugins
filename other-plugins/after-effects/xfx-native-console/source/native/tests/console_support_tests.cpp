#include "../src/console_support.h"
#include <windows.h>
#include <gdiplus.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <limits>
#include <fstream>
#include <winioctl.h>

namespace fs = std::filesystem;

struct TestGallery {
    fs::path root, gallery, outside;
    std::vector<fs::path> directory_links, file_links;
    TestGallery() {
        static unsigned int sequence = 0;
        root = fs::absolute(fs::temp_directory_path()) / (L"xfx-gallery-delete-test-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
        assert(fs::create_directory(root));
        gallery = root / L"gallery";
        outside = root / L"outside";
        fs::create_directory(gallery);
        fs::create_directory(outside);
    }
    ~TestGallery() {
        for (const auto& link : file_links) assert(DeleteFileW(link.c_str()));
        for (const auto& link : directory_links) assert(RemoveDirectoryW(link.c_str()));
        const auto resolved = fs::canonical(root);
        assert(resolved.parent_path() == fs::canonical(fs::temp_directory_path()));
        assert(resolved.filename().wstring().rfind(L"xfx-gallery-delete-test-", 0) == 0);
        fs::remove_all(resolved);
    }
};

static void write_file(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary);
    stream << "test fixture";
    assert(stream.good());
}

static bool create_junction(const fs::path& link, const fs::path& target) {
    // User-owned mount-point junctions do not require symlink privileges.
    struct MountPointBuffer {
        DWORD tag;
        WORD data_length, reserved;
        WORD substitute_offset, substitute_length, print_offset, print_length;
        wchar_t text[2048];
    } data{};
    const auto print = fs::absolute(target).wstring();
    const auto substitute = L"\\??\\" + print;
    assert(substitute.size() + print.size() + 2 < 2048);
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    data.print_length = static_cast<WORD>(print.size() * sizeof(wchar_t));
    data.data_length = static_cast<WORD>(8 + (substitute.size() + print.size() + 2) * sizeof(wchar_t));
    std::memcpy(data.text, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(data.text + substitute.size() + 1, print.c_str(), (print.size() + 1) * sizeof(wchar_t));
    if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
    HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.c_str()); return false; }
    DWORD returned = 0;
    const bool success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data,
        data.data_length + 8, nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    if (!success) RemoveDirectoryW(link.c_str());
    return success;
}

// Instrument only the Win32 boundary to deterministically exercise the gap
// between ancestor validation and opening the gallery directory.
static bool race_armed = false, race_swapped = false;
static fs::path race_gallery, race_parent, race_moved_parent, race_outside;
static HANDLE WINAPI gallery_test_create_file(LPCWSTR filename, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD attributes, HANDLE template_file) {
    if (race_armed && fs::path(filename) == race_gallery) {
        race_armed = false;
        assert(MoveFileExW(race_parent.c_str(), race_moved_parent.c_str(), 0));
        assert(create_junction(race_parent, race_outside));
        race_swapped = true;
    }
    return ::CreateFileW(filename, access, sharing, security, creation, attributes, template_file);
}
#define CreateFileW gallery_test_create_file
#include "../src/console_support.cpp"
#undef CreateFileW

static void snapshot_filename_uses_composition_name_and_png_extension() {
    // Catch replacing the composition name, dropping Unicode, or treating all dots as extensions.
    assert(xfx::snapshot_png_filename(L"片头 动画") == L"片头 动画.png");
    assert(xfx::snapshot_png_filename(L"合成.v2") == L"合成.v2.png");
    assert(xfx::snapshot_png_filename(L"封面.png") == L"封面.png");
    assert(xfx::snapshot_png_filename(L"封面.PNG") == L"封面.PNG");
    assert(xfx::snapshot_png_filename(L"星球\U0001f30d") == L"星球\U0001f30d.png");
    std::cout << "PASS snapshot filename preserves composition names and one PNG extension\n";
}

static void snapshot_filename_sanitizes_windows_leaf_names() {
    // Catch path/ADS characters escaping the selected folder and Win32 trailing-dot normalization.
    assert(xfx::snapshot_png_filename(L"A<>:\"/\\|?*B") == L"A_________B.png");
    assert(xfx::snapshot_png_filename(std::wstring(L"A\0B\tC\x001f" L"D", 7)) == L"A_B_C_D.png");
    assert(xfx::snapshot_png_filename(L"合成.  ") == L"合成.png");
    assert(xfx::snapshot_png_filename(L"合成.PNG . ") == L"合成.PNG");
    assert(xfx::snapshot_png_filename(L"") == L"合成快照.png");
    assert(xfx::snapshot_png_filename(L" . . ") == L"合成快照.png");
    assert(xfx::snapshot_png_filename(L"<>:\"/\\|?*\t") == L"合成快照.png");
    std::cout << "PASS snapshot filename sanitizes invalid characters and empty names\n";
}

static void snapshot_filename_avoids_dos_device_names() {
    // Catch bare devices and devices with an extension being offered to Win32 Save As.
    assert(xfx::snapshot_png_filename(L"CON") == L"_CON.png");
    assert(xfx::snapshot_png_filename(L"prn.PNG") == L"_prn.PNG");
    assert(xfx::snapshot_png_filename(L"AUX.txt") == L"_AUX.txt.png");
    assert(xfx::snapshot_png_filename(L"nul.v2.png") == L"_nul.v2.png");
    assert(xfx::snapshot_png_filename(L"COM1") == L"_COM1.png");
    assert(xfx::snapshot_png_filename(L"com9.png") == L"_com9.png");
    assert(xfx::snapshot_png_filename(L"LPT1.PNG") == L"_LPT1.PNG");
    assert(xfx::snapshot_png_filename(L"lpt9.txt") == L"_lpt9.txt.png");
    assert(xfx::snapshot_png_filename(L"COM¹") == L"_COM¹.png");
    assert(xfx::snapshot_png_filename(L"com².PNG") == L"_com².PNG");
    assert(xfx::snapshot_png_filename(L"COM³.v2") == L"_COM³.v2.png");
    assert(xfx::snapshot_png_filename(L"LPT¹") == L"_LPT¹.png");
    assert(xfx::snapshot_png_filename(L"lpt².png") == L"_lpt².png");
    assert(xfx::snapshot_png_filename(L"LPT³.tar.gz") == L"_LPT³.tar.gz.png");
    assert(xfx::snapshot_png_filename(L"COM⁴") == L"COM⁴.png");
    assert(xfx::snapshot_png_filename(L"CON .png") == L"_CON .png");
    assert(xfx::snapshot_png_filename(L"COM0") == L"COM0.png");
    assert(xfx::snapshot_png_filename(L"LPT10") == L"LPT10.png");
    assert(xfx::snapshot_png_filename(L"console") == L"console.png");
    std::cout << "PASS snapshot filename avoids reserved DOS basenames\n";
}

static void snapshot_filename_limits_leaf_without_splitting_unicode() {
    // 251 name units + four extension units is the 255-unit Windows leaf limit.
    assert(xfx::snapshot_png_filename(std::wstring(260, L'\u5408')) == std::wstring(251, L'\u5408') + L".png");
    assert(xfx::snapshot_png_filename(std::wstring(251, L'a') + L".PNG") == std::wstring(251, L'a') + L".PNG");
    // A surrogate pair beginning at unit 251 must be omitted, never cut in half.
    assert(xfx::snapshot_png_filename(std::wstring(250, L'a') + L"\U0001f30d") == std::wstring(250, L'a') + L".png");
    assert(xfx::snapshot_png_filename(std::wstring(249, L'a') + L"\U0001f30d") == std::wstring(249, L'a') + L"\U0001f30d.png");
    assert(xfx::snapshot_png_filename(std::wstring(250, L'a') + L".X") == std::wstring(250, L'a') + L".png");
    assert(xfx::snapshot_png_filename(std::wstring(251, L' ') + L"X") == L"合成快照.png");
    assert(xfx::snapshot_png_filename(L"CON" + std::wstring(249, L' ') + L"X") == L"_CON.png");
    std::cout << "PASS snapshot filename enforces a Unicode-safe Windows leaf limit\n";
}

static void delete_rejects_ancestor_swap_between_validation_and_open() {
    TestGallery fixture;
    race_parent = fixture.root / L"parent";
    race_moved_parent = fixture.root / L"saved-parent";
    race_gallery = race_parent / L"gallery";
    race_outside = fixture.outside;
    fs::create_directories(race_gallery);
    fs::create_directory(fixture.outside / L"gallery");
    write_file(race_gallery / L"victim.png");
    const auto external = fixture.outside / L"gallery" / L"victim.png";
    write_file(external);
    race_armed = true;
    race_swapped = false;
    const auto result = xfx::delete_captures(race_gallery, {race_gallery / L"victim.png"});
    assert(race_swapped);
    fixture.directory_links.push_back(race_parent);
    assert(!result[0].deleted && result[0].error != ERROR_SUCCESS);
    assert(fs::exists(external) && fs::exists(race_moved_parent / L"gallery" / L"victim.png"));
    std::cout << "PASS directory ancestor replacement cannot redirect a deletion outside the gallery\n";
}

static void delete_accepts_extended_length_directory_paths() {
    TestGallery fixture;
    const auto image = fixture.gallery / L"extended.png";
    write_file(image);
    const fs::path extended(L"\\\\?\\" + fixture.gallery.wstring());
    const auto result = xfx::delete_captures(extended, {extended / image.filename()});
    if (!result[0].deleted) std::cerr << "Extended path delete error: " << result[0].error << '\n';
    assert(result[0].deleted && result[0].error == ERROR_SUCCESS && !fs::exists(image));
    std::cout << "PASS direct PNG deletion supports extended-length Windows directory paths\n";
}

static void delete_direct_png_files() {
    TestGallery fixture;
    const auto first = fixture.gallery / L"中文.png", second = fixture.gallery / L"second.PNG";
    write_file(first);
    write_file(second);
    const auto result = xfx::delete_captures(fixture.gallery, {first, second.filename()});
    assert(result.size() == 2 && result[0].path == first);
    assert(result[0].deleted && result[1].deleted);
    assert(result[0].error == ERROR_SUCCESS && result[1].error == ERROR_SUCCESS);
    assert(!fs::exists(first) && !fs::exists(second));
    assert(xfx::delete_captures(fixture.gallery, {}).empty());
    std::cout << "PASS delete direct PNG files and accept a direct filename\n";
}

static void delete_rejects_invalid_batches_without_deleting_valid_files() {
    TestGallery fixture;
    const auto good = fixture.gallery / L"good.png";
    const auto external = fixture.outside / L"external.png";
    const auto nested = fixture.gallery / L"nested";
    fs::create_directory(nested);
    fs::create_directory(fixture.gallery / L"directory.png");
    write_file(good);
    write_file(external);
    write_file(nested / L"nested.png");
    write_file(fixture.gallery / L"note.txt");
    const std::vector<fs::path> invalid = {
        external, fixture.gallery / L".." / L"outside" / L"external.png",
        nested / L"nested.png", fixture.gallery / L"directory.png",
        fixture.gallery / L"missing.png", fixture.gallery / L"note.txt",
        fixture.gallery, fs::path(L"../outside/external.png"), fs::path(),
        fixture.gallery / L"." / L"good.png", fixture.gallery / L"good.png:stream.png"
    };
    for (const auto& bad : invalid) {
        const auto result = xfx::delete_captures(fixture.gallery, {good, bad});
        assert(result.size() == 2);
        assert(!result[0].deleted && !result[1].deleted);
        assert(result[0].error != ERROR_SUCCESS && result[1].error != ERROR_SUCCESS);
        assert(fs::exists(good) && fs::exists(external) && fs::exists(nested / L"nested.png"));
    }
    const auto duplicate = xfx::delete_captures(fixture.gallery, {good, good});
    assert(!duplicate[0].deleted && !duplicate[1].deleted && fs::exists(good));
    std::cout << "PASS delete rejects traversal, outside paths, nested files, directories, ADS, and invalid batches\n";
}

static void delete_rejects_symlinks_and_junctions() {
    TestGallery fixture;
    const auto good = fixture.gallery / L"good.png", external = fixture.outside / L"external.png";
    write_file(good);
    write_file(external);
    const auto symbolic = fixture.gallery / L"symbolic.png";
    BOOLEAN linked = CreateSymbolicLinkW(symbolic.c_str(), external.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
    DWORD link_error = linked ? ERROR_SUCCESS : GetLastError();
    if (!linked && link_error == ERROR_INVALID_PARAMETER) {
        linked = CreateSymbolicLinkW(symbolic.c_str(), external.c_str(), 0);
        link_error = linked ? ERROR_SUCCESS : GetLastError();
    }
    if (!linked && (link_error == ERROR_PRIVILEGE_NOT_HELD || link_error == ERROR_ACCESS_DENIED ||
                    link_error == ERROR_NOT_SUPPORTED)) {
        std::cout << "SKIP symbolic-link deletion guard: Windows denied test-link creation ("
                  << link_error << "); continuing with junction guards\n";
    } else {
        assert(linked);
        fixture.file_links.push_back(symbolic);
        auto result = xfx::delete_captures(fixture.gallery, {good, symbolic});
        assert(!result[0].deleted && !result[1].deleted && result[1].error != ERROR_SUCCESS);
        assert(fs::exists(external) && fs::exists(symbolic) && fs::exists(good));
    }
    const auto junction = fixture.gallery / L"junction.png";
    assert(create_junction(junction, fixture.outside));
    fixture.directory_links.push_back(junction);
    auto result = xfx::delete_captures(fixture.gallery, {good, junction});
    assert(!result[0].deleted && !result[1].deleted && result[1].error != ERROR_SUCCESS);
    const auto root_link = fixture.root / L"root-junction";
    assert(create_junction(root_link, fixture.outside));
    fixture.directory_links.push_back(root_link);
    result = xfx::delete_captures(root_link, {root_link / L"external.png"});
    assert(!result[0].deleted && result[0].error != ERROR_SUCCESS);
    fs::create_directory(fixture.outside / L"nested-gallery");
    const auto nested_external = fixture.outside / L"nested-gallery" / L"nested-external.png";
    write_file(nested_external);
    result = xfx::delete_captures(root_link / L"nested-gallery", {root_link / L"nested-gallery" / L"nested-external.png"});
    assert(!result[0].deleted && result[0].error != ERROR_SUCCESS && fs::exists(nested_external));
    assert(fs::exists(external) && fs::exists(good));
    std::cout << "PASS delete rejects file symlinks, junction entries, and junction gallery roots/ancestors\n";
}

static void delete_reports_individual_filesystem_failures() {
    TestGallery fixture;
    const auto good = fixture.gallery / L"good.png", readonly = fixture.gallery / L"readonly.png";
    write_file(good);
    write_file(readonly);
    assert(SetFileAttributesW(readonly.c_str(), FILE_ATTRIBUTE_READONLY));
    const auto result = xfx::delete_captures(fixture.gallery, {good, readonly});
    assert(result[0].deleted && result[0].error == ERROR_SUCCESS && !fs::exists(good));
    assert(!result[1].deleted && result[1].error != ERROR_SUCCESS && fs::exists(readonly));
    assert(SetFileAttributesW(readonly.c_str(), FILE_ATTRIBUTE_NORMAL));
    std::cout << "PASS delete reports per-file filesystem errors after prevalidation\n";
}

static void delete_keeps_locked_file_failure_local() {
    TestGallery fixture;
    const auto good = fixture.gallery / L"good.png", locked = fixture.gallery / L"locked.png";
    write_file(good);
    write_file(locked);
    HANDLE reader = CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(reader != INVALID_HANDLE_VALUE);
    const auto result = xfx::delete_captures(fixture.gallery, {good, locked});
    assert(result[0].deleted && result[0].error == ERROR_SUCCESS && !fs::exists(good));
    assert(!result[1].deleted && result[1].error == ERROR_SHARING_VIOLATION && fs::exists(locked));
    CloseHandle(reader);
    std::cout << "PASS locked PNG fails individually without blocking another valid deletion\n";
}

static void dib_payload_preserves_rows_and_alpha() {
    // Two distinct rows expose accidental bottom-up packing or RGBA conversion.
    const std::vector<std::uint8_t> pixels = {
        56, 34, 12, 255, 90, 80, 70, 128,
        5, 6, 7, 0, 11, 22, 33, 64
    };
    const auto dib = xfx::pack_bgra_dib(2, 2, pixels);
    assert(dib.size() == sizeof(BITMAPV5HEADER) + 16);
    BITMAPV5HEADER header{};
    std::memcpy(&header, dib.data(), sizeof(header));
    assert(header.bV5Size == sizeof(BITMAPV5HEADER));
    assert(header.bV5Width == 2 && header.bV5Height == -2);
    assert(header.bV5Planes == 1 && header.bV5BitCount == 32);
    assert(header.bV5Compression == BI_BITFIELDS && header.bV5SizeImage == 16);
    assert(header.bV5RedMask == 0x00ff0000 && header.bV5GreenMask == 0x0000ff00);
    assert(header.bV5BlueMask == 0x000000ff && header.bV5AlphaMask == 0xff000000);
    assert(header.bV5CSType == LCS_sRGB);
    assert(std::memcmp(dib.data() + sizeof(header), pixels.data(), pixels.size()) == 0);
    std::cout << "PASS DIB top-down BGRA pixels and alpha\n";
}

static void dib_payload_rejects_invalid_input() {
    const std::vector<std::uint8_t> pixel = {1, 2, 3, 4};
    assert(xfx::pack_bgra_dib(0, 1, {}).empty());
    assert(xfx::pack_bgra_dib(1, 0, {}).empty());
    assert(xfx::pack_bgra_dib(1, 1, {}).empty());
    assert(xfx::pack_bgra_dib(2, 1, pixel).empty());
    assert(xfx::pack_bgra_dib(1, 1, std::vector<std::uint8_t>(5)).empty());
    assert(xfx::pack_bgra_dib((std::numeric_limits<std::uint32_t>::max)(), 2, {}).empty());
    assert(xfx::pack_bgra_dib(65536, 65536, {}).empty());
    // Invalid owner returns before OpenClipboard; tests never change the user's clipboard.
    assert(!xfx::copy_capture_to_clipboard(nullptr, L"missing.png"));
    std::cout << "PASS DIB rejects invalid dimensions, lengths, and owner\n";
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    snapshot_filename_uses_composition_name_and_png_extension();
    snapshot_filename_sanitizes_windows_leaf_names();
    snapshot_filename_avoids_dos_device_names();
    snapshot_filename_limits_leaf_without_splitting_unicode();
    dib_payload_preserves_rows_and_alpha();
    dib_payload_rejects_invalid_input();
    delete_direct_png_files();
    delete_rejects_ancestor_swap_between_validation_and_open();
    delete_accepts_extended_length_directory_paths();
    delete_rejects_invalid_batches_without_deleting_valid_files();
    delete_rejects_symlinks_and_junctions();
    delete_reports_individual_filesystem_failures();
    delete_keeps_locked_file_failure_local();
    assert(xfx::parse_script_reply("OK\t2").succeeded);
    assert(xfx::parse_script_reply("PARTIAL\t1\tlocked layer").count == 1);
    assert(!xfx::parse_script_reply("PARTIAL\t1\tlocked layer").succeeded);
    assert(!xfx::parse_script_reply("OK\tabc").succeeded);
    assert(!xfx::parse_script_reply("").succeeded);
    assert(!xfx::should_close_console(false, false, 1));
    // Focus loss is unconditional now: the legacy auto-close/fixed-window
    // switches no longer change the product behavior.
    assert(xfx::should_close_console(false, false, 0));
    assert(xfx::should_close_console(true, true, 0));
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    assert(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok);
    {
        const auto dir = std::filesystem::temp_directory_path() /
            (L"xfx-gallery-test-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        Gdiplus::Bitmap bitmap(3, 2, PixelFormat32bppARGB);
        bitmap.SetPixel(0, 0, Gdiplus::Color(255, 12, 34, 56));
        bitmap.SetPixel(1, 0, Gdiplus::Color(0, 0, 0, 0));
        bitmap.SetPixel(2, 1, Gdiplus::Color(128, 11, 22, 33));
        CLSID png;
        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &png);
        const auto original = dir / L"中文快照.png";
        assert(bitmap.Save(original.c_str(), &png) == Gdiplus::Ok);
        const auto loaded_dib = xfx::load_capture_dib(original);
        assert(loaded_dib.size() == sizeof(BITMAPV5HEADER) + 24);
        const auto* loaded_pixels = loaded_dib.data() + sizeof(BITMAPV5HEADER);
        assert(loaded_pixels[0] == 56 && loaded_pixels[1] == 34 && loaded_pixels[2] == 12 && loaded_pixels[3] == 255);
        assert(loaded_pixels[7] == 0);
        assert(loaded_pixels[20] == 33 && loaded_pixels[21] == 22 && loaded_pixels[22] == 11 && loaded_pixels[23] == 128);
        assert(xfx::load_capture_dib(dir / L"missing.png").empty());
        unsigned int argb = 0;
        assert(xfx::sample_image_pixel(original, 0, 0, argb));
        assert(argb == 0xff0c2238);
        assert(!xfx::sample_image_pixel(original, 3, 0, argb));
        assert(xfx::export_capture(original, dir / L"透明另存.png", false));
        assert(xfx::sample_image_pixel(dir / L"透明另存.png", 1, 0, argb));
        assert((argb >> 24) == 0);
        assert(xfx::export_capture(original, dir / L"白底.jpg", true));
        {
            Gdiplus::Bitmap jpeg((dir / L"白底.jpg").c_str());
            assert(jpeg.GetWidth() == 3 && jpeg.GetHeight() == 2);
        }
        assert(xfx::scan_captures(dir).size() == 2); // Gallery only indexes PNG originals.
        assert(!xfx::export_capture(dir / L"missing.png", dir / L"fail.jpg", true));
        std::filesystem::remove_all(dir);
    }
    Gdiplus::GdiplusShutdown(token);
    std::cout << "console support tests passed\n";
}
