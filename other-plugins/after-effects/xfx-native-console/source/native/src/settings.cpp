#include "settings.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace xfx {
namespace {

std::wstring decode_settings(const std::string& raw) {
    const bool bom = raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0;
    const char* data = raw.data() + (bom ? 3 : 0);
    const size_t size = raw.size() - (bom ? 3 : 0);
    if (!size) return {};
    if (size > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("配置文件过大。");
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int count = MultiByteToWideChar(code_page, flags, data, static_cast<int>(size), nullptr, 0);
    if (!count) {
        // Previous releases used the current system locale, commonly GBK on Chinese Windows.
        code_page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(code_page, flags, data, static_cast<int>(size), nullptr, 0);
    }
    if (!count) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "无法解码配置文件");
    std::wstring decoded(static_cast<size_t>(count), L'\0');
    if (!MultiByteToWideChar(code_page, flags, data, static_cast<int>(size), decoded.data(), count))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "无法解码配置文件");
    return decoded;
}

std::string encode_settings(const std::wstring& text) {
    if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("配置内容过大。");
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count && !text.empty())
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "配置含有无效 Unicode 字符");
    std::string encoded = "\xEF\xBB\xBF";
    encoded.resize(static_cast<size_t>(count) + 3);
    if (count && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), encoded.data() + 3, count, nullptr, nullptr))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "无法将配置编码为 UTF-8");
    return encoded;
}

void validate_field(const std::wstring& value, bool tab_delimited_key = false) {
    if (value.find_first_of(L"\r\n") != std::wstring::npos || value.find(L'\0') != std::wstring::npos ||
        (tab_delimited_key && value.find(L'\t') != std::wstring::npos)) {
        throw std::invalid_argument("配置字段包含换行、空字符或无效的分隔符。");
    }
}

void validate_settings(const Settings& settings) {
    validate_field(settings.hotkey);
    validate_field(settings.capture_directory, true);
    if (!settings.capture_directory.empty() && !std::filesystem::path(settings.capture_directory).is_absolute())
        throw std::invalid_argument("快照保存位置必须是绝对路径。");
    for (const auto& value : settings.favorites) validate_field(value);
    for (const auto& value : settings.pinned) validate_field(value);
    for (const auto& value : settings.recent) validate_field(value);
    for (const auto& entry : settings.overrides) {
        validate_field(entry.first, true);
        validate_field(entry.second);
    }
    for (const auto& value : settings.slots) validate_field(value);
    for (const auto& value : settings.slot_labels) validate_field(value);
    for (const auto& entry : settings.custom_aliases) {
        validate_field(entry.first, true);
        for (const auto& alias : entry.second) validate_field(alias);
    }
}

void atomic_write(const std::filesystem::path& path, const std::string& bytes) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    if (std::filesystem::is_directory(path)) throw std::runtime_error("配置目标是目录，无法保存。");
    static volatile LONG sequence = 0;
    const std::wstring suffix = L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(InterlockedIncrement(&sequence));
    const std::filesystem::path temporary = path.wstring() + suffix;
    HANDLE output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "无法创建配置临时文件");
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            const DWORD chunk = static_cast<DWORD>((std::min)(bytes.size() - offset, static_cast<size_t>(MAXDWORD)));
            DWORD written = 0;
            if (!WriteFile(output, bytes.data() + offset, chunk, &written, nullptr) || written != chunk)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "写入配置临时文件失败");
            offset += written;
        }
        if (!FlushFileBuffers(output))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "刷新配置临时文件失败");
        if (!CloseHandle(output)) {
            output = INVALID_HANDLE_VALUE;
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "关闭配置临时文件失败");
        }
        output = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "替换配置文件失败，原配置已保留");
    } catch (...) {
        if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
        DeleteFileW(temporary.c_str());
        throw;
    }
}

} // namespace

Settings SettingsStore::load(const std::filesystem::path& path, bool required) {
    Settings settings;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        if (required || exists || error) throw std::runtime_error("无法读取配置文件，当前配置已保留。");
        return settings;
    }
    const std::string raw(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) throw std::runtime_error("读取配置文件失败。");
    std::wistringstream lines(decode_settings(raw));
    std::wstring line;
    bool explicit_theme_mode = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.rfind(L"UseEnglishMode=", 0) == 0) {
            settings.use_english_mode = line.substr(15) == L"1";
        } else if (line.rfind(L"FixedWindow=", 0) == 0) {
            settings.fixed_window = line.substr(12) == L"1";
        } else if (line.rfind(L"AutoClose=", 0) == 0) {
            settings.auto_close = line.substr(10) != L"0";
        } else if (line.rfind(L"LightTheme=", 0) == 0) {
            settings.light_theme = line.substr(11) == L"1";
            if (!explicit_theme_mode) settings.theme_mode = settings.light_theme ? ThemeMode::Light : ThemeMode::Dark;
        } else if (line.rfind(L"ThemeMode=", 0) == 0) {
            const auto mode = line.substr(10);
            if (mode != L"follow" && mode != L"light" && mode != L"dark")
                throw std::invalid_argument("界面主题必须是 follow、light 或 dark。");
            settings.theme_mode = mode == L"light" ? ThemeMode::Light : mode == L"dark" ? ThemeMode::Dark : ThemeMode::Follow;
            explicit_theme_mode = true;
        } else if (line.rfind(L"FullResolutionScreenshots=", 0) == 0) {
            // Kept as a compatibility record; snapshots are always full resolution.
            settings.full_resolution_screenshots = true;
        } else if (line.rfind(L"CaptureDirectory=", 0) == 0) {
            const auto directory = line.substr(17);
            validate_field(directory, true);
            if (!directory.empty() && !std::filesystem::path(directory).is_absolute())
                throw std::invalid_argument("快照保存位置必须是绝对路径。");
            settings.capture_directory = directory;
        } else if (line.rfind(L"UseEnglishInputMethod=", 0) == 0) {
            settings.use_english_mode = line.substr(22) == L"1";
        } else if (line.rfind(L"Hotkey=", 0) == 0) {
            settings.hotkey = line.substr(7);
        } else if (line.rfind(L"Favorite=", 0) == 0) {
            settings.favorites.push_back(line.substr(9));
        } else if (line.rfind(L"Pinned=", 0) == 0) {
            settings.pinned.push_back(line.substr(7));
        } else if (line.rfind(L"Recent=", 0) == 0) {
            settings.recent.push_back(line.substr(7));
        } else if (line.rfind(L"Override=", 0) == 0) {
            const size_t separator = line.find(L'\t', 9);
            if (separator != std::wstring::npos) settings.overrides[line.substr(9, separator - 9)] = line.substr(separator + 1);
        } else if (line.rfind(L"Alias=", 0) == 0) {
            const size_t separator = line.find(L'\t', 6);
            if (separator == std::wstring::npos) settings.custom_aliases.try_emplace(line.substr(6));
            else settings.custom_aliases[line.substr(6, separator - 6)].push_back(line.substr(separator + 1));
        } else if (line.rfind(L"SlotLabel", 0) == 0 && line.size() >= 11 && line[10] == L'=') {
            const wchar_t slot = line[9];
            if (slot >= L'1' && slot <= L'9') settings.slot_labels[static_cast<size_t>(slot - L'1')] = line.substr(11);
        } else if (line.rfind(L"Slot", 0) == 0 && line.size() >= 6 && line[5] == L'=') {
            const wchar_t slot = line[4];
            if (slot >= L'1' && slot <= L'9') settings.slots[static_cast<size_t>(slot - L'1')] = line.substr(6);
        }
    }
    return settings;
}

void SettingsStore::save(const std::filesystem::path& path, const Settings& settings) {
    validate_settings(settings);
    std::wostringstream output;
    output << L"UseEnglishMode=" << (settings.use_english_mode ? L"1" : L"0") << L"\r\n";
    output << L"FixedWindow=" << (settings.fixed_window ? L"1" : L"0") << L"\r\n";
    output << L"AutoClose=" << (settings.auto_close ? L"1" : L"0") << L"\r\n";
    output << L"LightTheme=" << (settings.light_theme ? L"1" : L"0") << L"\r\n";
    output << L"ThemeMode=" << (settings.theme_mode == ThemeMode::Follow ? L"follow" :
        settings.theme_mode == ThemeMode::Light ? L"light" : L"dark") << L"\r\n";
    output << L"FullResolutionScreenshots=1\r\n";
    output << L"CaptureDirectory=" << settings.capture_directory << L"\r\n";
    output << L"Hotkey=" << settings.hotkey << L"\r\n";
    for (const auto& favorite : settings.favorites) output << L"Favorite=" << favorite << L"\r\n";
    for (const auto& pinned : settings.pinned) output << L"Pinned=" << pinned << L"\r\n";
    for (const auto& recent : settings.recent) output << L"Recent=" << recent << L"\r\n";
    for (const auto& entry : settings.overrides) output << L"Override=" << entry.first << L'\t' << entry.second << L"\r\n";
    for (size_t index = 0; index < settings.slots.size(); ++index) {
        if (!settings.slots[index].empty()) output << L"Slot" << (index + 1) << L"=" << settings.slots[index] << L"\r\n";
        if (!settings.slot_labels[index].empty()) output << L"SlotLabel" << (index + 1) << L"=" << settings.slot_labels[index] << L"\r\n";
    }
    for (const auto& entry : settings.custom_aliases) {
        if (entry.second.empty()) output << L"Alias=" << entry.first << L"\r\n";
        for (const auto& alias : entry.second) output << L"Alias=" << entry.first << L'\t' << alias << L"\r\n";
    }
    atomic_write(path, encode_settings(output.str()));
}

void SettingsStore::export_file(const std::filesystem::path& source, const std::filesystem::path& target) {
    // Normalize through the current model so removed legacy records are not
    // copied into an exported configuration.
    save(target, load(source, true));
}

} // namespace xfx
