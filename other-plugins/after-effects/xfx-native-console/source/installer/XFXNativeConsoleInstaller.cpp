#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define IDR_XFX_PLUGIN 101
#define IDR_XFX_ALIASES 102
#define IDR_XFX_NOTICES 103
#define IDR_XFX_PANEL 104
namespace fs = std::filesystem;

struct install_payload { std::vector<char> plugin; std::vector<char> aliases; std::vector<char> notices; std::vector<char> panel; };
struct installer_options {
    fs::path target, log;
    bool silent = false, list = false, help = false;
#ifdef XFX_UNINSTALLER
    bool uninstall = true;
#else
    bool uninstall = false;
#endif
};

std::wstring wide_text(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (length) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}
std::string utf8_text(const std::wstring& text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(length), '\0');
    if (length) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::wstring install_suffix() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t text[96]{};
    swprintf_s(text, L"%04d%02d%02d-%02d%02d%02d-%03d-%lu-%llu", time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentProcessId(), GetTickCount64());
    return text;
}
bool valid_after_effects_root(const fs::path& root) {
    std::error_code error;
    const bool executable = fs::is_regular_file(root / L"AfterFX.exe", error) || fs::is_regular_file(root / L"AfterFXBeta.exe", error);
    return executable && fs::is_directory(root / L"Plug-ins", error);
}
bool is_after_effects_process_name(const std::wstring& name) {
    return _wcsicmp(name.c_str(), L"AfterFX.exe") == 0 || _wcsicmp(name.c_str(), L"AfterFX.com") == 0 ||
           _wcsicmp(name.c_str(), L"AfterFXBeta.exe") == 0 || _wcsicmp(name.c_str(), L"AfterFXBeta.com") == 0 ||
           _wcsicmp(name.c_str(), L"AfterFXa.exe") == 0;
}
std::vector<DWORD> after_effects_process_ids() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("无法检查 AE 是否正在运行，请稍后重试。");
    PROCESSENTRY32W entry = {sizeof(PROCESSENTRY32W)};
    std::vector<DWORD> processes;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (is_after_effects_process_name(entry.szExeFile)) {
                processes.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) { CloseHandle(snapshot); throw std::runtime_error("无法完整检查 AE 进程，请稍后重试。"); }
    } else if (GetLastError() != ERROR_NO_MORE_FILES) { CloseHandle(snapshot); throw std::runtime_error("无法读取进程列表，请稍后重试。"); }
    CloseHandle(snapshot);
    return processes;
}
bool is_after_effects_running() {
    return !after_effects_process_ids().empty();
}
void require_after_effects_closed() {
    if (is_after_effects_running())
        throw std::runtime_error("After Effects 正在运行。请先切回 AE 保存所有工程，再自行退出所有 AE，然后重新运行安装或卸载程序。本程序不会关闭 AE，也未修改插件文件。");
}
void add_candidate(std::vector<fs::path>& roots, fs::path candidate) {
    if (candidate.empty()) return;
    if (_wcsicmp(candidate.filename().c_str(), L"Plug-ins") == 0) candidate = candidate.parent_path();
    if (!valid_after_effects_root(candidate)) candidate /= L"Support Files";
    if (!valid_after_effects_root(candidate)) return;
    candidate = fs::weakly_canonical(candidate);
    if (std::none_of(roots.begin(), roots.end(), [&](const fs::path& root) { return _wcsicmp(root.c_str(), candidate.c_str()) == 0; }))
        roots.push_back(candidate);
}
void find_registry_roots(HKEY hive, REGSAM view, std::vector<fs::path>& roots) {
    HKEY base = nullptr;
    if (RegOpenKeyExW(hive, L"SOFTWARE\\Adobe\\After Effects", 0, KEY_READ | view, &base) != ERROR_SUCCESS) return;
    for (DWORD index = 0;; ++index) {
        wchar_t version[256]{};
        DWORD size = 256;
        const LONG status = RegEnumKeyExW(base, index, version, &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) continue;
        HKEY key = nullptr;
        if (RegOpenKeyExW(base, version, 0, KEY_READ | view, &key) != ERROR_SUCCESS) continue;
        for (const wchar_t* name : {L"PluginInstallPath", L"InstallPath", L"Path"}) {
            wchar_t value[32768]{};
            DWORD bytes = sizeof(value), type = 0;
            if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value), &bytes) != ERROR_SUCCESS ||
                (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
            value[32767] = L'\0';
            std::wstring text(value);
            if (type == REG_EXPAND_SZ) {
                wchar_t expanded[32768]{};
                const DWORD length = ExpandEnvironmentStringsW(value, expanded, 32768);
                if (length && length <= 32768) text = expanded;
            }
            while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
            add_candidate(roots, text);
        }
        RegCloseKey(key);
    }
    RegCloseKey(base);
}
std::vector<fs::path> find_after_effects_roots() {
    std::vector<fs::path> roots;
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        find_registry_roots(hive, KEY_WOW64_64KEY, roots);
        find_registry_roots(hive, KEY_WOW64_32KEY, roots);
    }
    wchar_t program_files[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, program_files) == S_OK) {
        const fs::path adobe = fs::path(program_files) / L"Adobe";
        std::error_code error;
        if (fs::is_directory(adobe, error)) {
            for (const auto& entry : fs::directory_iterator(adobe, error)) {
                if (entry.path().filename().wstring().rfind(L"Adobe After Effects", 0) == 0)
                    add_candidate(roots, entry.path() / L"Support Files");
            }
        }
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}
std::vector<char> read_resource(WORD id) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) throw std::runtime_error("安装包资源缺失，请重新获取完整安装器。");
    const HGLOBAL loaded = LoadResource(nullptr, resource);
    const DWORD size = SizeofResource(nullptr, resource);
    const char* data = static_cast<const char*>(loaded ? LockResource(loaded) : nullptr);
    if (!data || !size) throw std::runtime_error("安装包资源读取失败。");
    return std::vector<char>(data, data + size);
}
struct file_change {
    fs::path target, stage, backup;
    bool old_moved = false, new_moved = false;
};
void write_stage(const fs::path& path, const std::vector<char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("无法写入安装临时文件：" + path.u8string());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) throw std::runtime_error("写入安装临时文件失败：" + path.u8string());
}
// One transaction spans all selected versions; all payloads are staged before any installed file moves.
void install_files(const std::vector<fs::path>& roots, const install_payload& payload) {
    require_after_effects_closed();
    if (roots.empty()) throw std::runtime_error("没有找到可安装的 AE 目录。可用 --target 指定 Support Files 目录。");
    if (payload.plugin.empty() || payload.aliases.empty() || payload.panel.empty())
        throw std::runtime_error("安装包中插件、默认词库或面板脚本为空。");
    for (const auto& root : roots) {
        if (!valid_after_effects_root(root))
            throw std::runtime_error("目标不是有效 AE Support Files 目录（需含 AfterFX.exe 和 Plug-ins）：" + root.u8string());
    }
    const std::wstring suffix = install_suffix();
    std::vector<file_change> changes;
    std::vector<fs::path> created_directories;
    for (const auto& root : roots) {
        for (const wchar_t* name : {L"XFXNativeConsole.aex", L"XFXNativeConsole.aliases.default.tsv"}) {
            const fs::path target = root / L"Plug-ins" / name;
            changes.push_back({target, target.wstring() + L".stage-" + suffix, target.wstring() + L".backup-" + suffix});
        }
        if (!payload.notices.empty()) {
            const fs::path target = root / L"Plug-ins" / L"XFXNativeConsole.THIRD-PARTY-NOTICES.txt";
            changes.push_back({target, target.wstring() + L".stage-" + suffix, target.wstring() + L".backup-" + suffix});
        }
        const fs::path scripts = root / L"Scripts" / L"ScriptUI Panels";
        const fs::path panel = scripts / L"效果控制台.jsx";
        changes.push_back({panel, panel.wstring() + L".stage-" + suffix, panel.wstring() + L".backup-" + suffix});
        if (fs::is_directory(scripts)) {
            for (const auto& entry : fs::directory_iterator(scripts)) {
                const std::wstring name = entry.path().filename().wstring();
                if (entry.is_regular_file() && (name == L"XFXEnhancerBridge.jsx" || name == L"XFXEnhancerBridge.jsxbin"))
                    changes.push_back({entry.path(), {}, entry.path().wstring() + L".disabled-" + suffix});
                else if (entry.is_regular_file() && name == L"XFX 中文控制台.jsx")
                    changes.push_back({entry.path(), {}, entry.path().wstring() + L".backup-" + suffix});
            }
        }
    }
    try {
        for (const auto& root : roots) {
            for (const auto& directory : {root / L"Scripts", root / L"Scripts" / L"ScriptUI Panels"}) {
                if (fs::create_directory(directory)) created_directories.push_back(directory);
            }
        }
        for (const auto& change : changes) {
            if (!change.stage.empty())
                write_stage(change.stage, change.target.extension() == L".aex" ? payload.plugin :
                    (change.target.extension() == L".txt" ? payload.notices :
                        (change.target.extension() == L".jsx" ? payload.panel : payload.aliases)));
        }
        for (auto& change : changes) {
            require_after_effects_closed();
            if (fs::exists(change.target)) {
                if (!fs::is_regular_file(change.target)) throw std::runtime_error("安装目标被同名目录占用：" + change.target.u8string());
                fs::rename(change.target, change.backup);
                change.old_moved = true;
            }
            if (!change.stage.empty()) {
                fs::rename(change.stage, change.target);
                change.new_moved = true;
            }
        }
    } catch (const std::exception& original) {
        const std::string failure = original.what();
        std::string rollback_errors;
        for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
            try {
                if (it->new_moved) fs::remove(it->target);
                if (it->old_moved) fs::rename(it->backup, it->target);
                if (!it->stage.empty() && fs::exists(it->stage)) fs::remove(it->stage);
            } catch (const std::exception& error) { rollback_errors += "\n" + std::string(error.what()); }
        }
        for (auto it = created_directories.rbegin(); it != created_directories.rend(); ++it) {
            try { fs::remove(*it); } // Empty only; never recursively removes a user directory.
            catch (const std::exception& error) { rollback_errors += "\n" + std::string(error.what()); }
        }
        throw std::runtime_error(failure + (rollback_errors.empty() ? "\n已回滚本次文件改动。" : "\n部分回滚失败，请保留备份并查看日志：" + rollback_errors));
    }
}
// Reject links/junctions at every existing ancestor before removing any known file.
void reject_reparse_path(const fs::path& path) {
    fs::path current;
    for (const auto& part : fs::absolute(path).lexically_normal()) {
        current /= part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
            throw std::runtime_error("无法核对卸载路径：" + current.u8string());
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("卸载目录含符号链接或目录联接，请使用真实 AE 安装目录：" + current.u8string());
    }
}
size_t uninstall_files(const std::vector<fs::path>& roots) {
    require_after_effects_closed();
    if (roots.empty()) throw std::runtime_error("没有找到有效 AE 目录，可用 --target 指定 Support Files。");
    struct removal { fs::path target, stage; bool moved = false; };
    std::vector<removal> files;
    const std::wstring suffix = install_suffix();
    for (const auto& root : roots) {
        if (!valid_after_effects_root(root)) throw std::runtime_error("卸载目标不是有效 AE Support Files 目录：" + root.u8string());
        for (const auto& relative : {fs::path(L"Plug-ins/XFXNativeConsole.aex"), fs::path(L"Plug-ins/XFXNativeConsole.aliases.default.tsv"),
            fs::path(L"Plug-ins/XFXNativeConsole.THIRD-PARTY-NOTICES.txt"), fs::path(L"Scripts/ScriptUI Panels/效果控制台.jsx"),
            fs::path(L"Scripts/ScriptUI Panels/XFX 中文控制台.jsx")}) {
            const auto target = fs::absolute(root / relative).lexically_normal();
            reject_reparse_path(target);
            if (!fs::exists(target)) continue;
            if (!fs::is_regular_file(target) || (GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_READONLY))
                throw std::runtime_error("卸载文件被目录占用或只读，请检查：" + target.u8string());
            if (std::any_of(files.begin(), files.end(), [&](const removal& f) { return _wcsicmp(f.target.c_str(), target.c_str()) == 0; })) continue;
            files.push_back({target, target.wstring() + L".uninstalled-" + suffix});
        }
    }
    try {
        for (auto& file : files) {
            require_after_effects_closed();
            fs::rename(file.target, file.stage);
            file.moved = true;
        }
    } catch (const std::exception& error) {
        std::string failure = error.what();
        for (auto i = files.rbegin(); i != files.rend(); ++i) {
            if (i->moved) { try { fs::rename(i->stage, i->target); } catch (const std::exception& e) { failure += "\n恢复失败：" + std::string(e.what()); } }
        }
        throw std::runtime_error(failure + "\n卸载未完成，已尝试恢复全部原文件。");
    }
    std::string failures;
    for (const auto& file : files) {
        try { fs::remove(file.stage); }
        catch (const std::exception&) { failures += "\n" + file.stage.u8string(); }
    }
    if (!failures.empty()) throw std::runtime_error("插件已停用，但下列卸载暂存文件未能删除，请保留日志并手动清理：" + failures);
    return files.size();
}
fs::path default_log_path() {
    wchar_t documents[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents) == S_OK)
        return fs::path(documents) / L"LoYoi" / L"XFXNative" / L"install.log";
    return fs::temp_directory_path() / L"XFXNativeConsole-install.log";
}
void append_install_log(const fs::path& path, const std::wstring& message) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream) throw std::runtime_error("无法写入安装日志：" + path.u8string());
    stream << utf8_text(install_suffix() + L" " + message) << "\r\n";
    stream.close();
    if (!stream) throw std::runtime_error("写入安装日志失败：" + path.u8string());
}
installer_options parse_options(const std::vector<std::wstring>& arguments) {
    installer_options options;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == L"--silent" || argument == L"/quiet") options.silent = true;
        else if (argument == L"--list") options.list = true;
        else if (argument == L"--help" || argument == L"/?") options.help = true;
        else if (argument == L"--uninstall") options.uninstall = true;
        else if (argument == L"--target" || argument == L"--log") {
            if (++i == arguments.size() || arguments[i].rfind(L"--", 0) == 0)
                throw std::runtime_error("命令行参数缺少路径：" + utf8_text(argument));
            if (argument == L"--target") options.target = arguments[i]; else options.log = arguments[i];
        } else throw std::runtime_error("无法识别命令行参数：" + utf8_text(argument));
    }
    return options;
}
#ifndef XFX_INSTALLER_TEST
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    installer_options options;
    fs::path log = default_log_path();
    try {
        int count = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!argv) throw std::runtime_error("无法读取安装参数。");
        const std::vector<std::wstring> arguments(argv + 1, argv + count);
        LocalFree(argv);
        options.silent = std::find(arguments.begin(), arguments.end(), L"--silent") != arguments.end() ||
                         std::find(arguments.begin(), arguments.end(), L"/quiet") != arguments.end();
        options = parse_options(arguments);
        if (!options.log.empty()) log = fs::absolute(options.log);
        if (options.help) {
            const wchar_t* help = L"效果控制台 1.0.19 安装 / 卸载工具\n\n--target <AE Support Files 目录>  指定目标\n--uninstall  卸载本插件，保留个人设置与备份\n--silent  不弹提示框\n--list  只列出目标，不修改文件\n--log <文件路径>  指定 UTF-8 日志\n\nAE 正在运行时会停止并提示先保存工程、自行退出后重试。程序不会关闭 AE。\n独立卸载器默认执行卸载。未指定目标时处理全部检测到的有效目录。\n需要管理员权限。退出码：0 成功，1 未完成。";
            std::wcout << help << std::endl;
            if (!options.silent) MessageBoxW(nullptr, help, L"效果控制台", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        append_install_log(log, L"安装器启动");
        const auto roots = options.target.empty() ? find_after_effects_roots() : std::vector<fs::path>{fs::absolute(options.target)};
        std::wstring targets;
        for (const auto& root : roots) targets += L"\n" + root.wstring();
        append_install_log(log, L"目标目录：" + targets);
        if (options.list) {
            const std::wstring message = roots.empty() ? L"没有检测到有效 AE 目录。" : L"检测到的 AE 目录：" + targets;
            std::wcout << message << std::endl;
            if (!options.silent) MessageBoxW(nullptr, message.c_str(), L"效果控制台", MB_OK | MB_ICONINFORMATION);
            return roots.empty() ? 1 : 0;
        }
        require_after_effects_closed();
        if (options.uninstall) {
            if (!options.silent && MessageBoxW(nullptr,
                (L"将从以下 AE 目录卸载效果控制台：" + targets + L"\n\n个人设置、用户词库和备份会保留。\n是否继续？").c_str(),
                L"卸载效果控制台", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return 1;
            const size_t removed = uninstall_files(roots);
            const std::wstring message = L"效果控制台卸载完成。\n已移除文件数：" + std::to_wstring(removed) +
                L"\n个人设置、用户词库、旧快照和备份均保留。\n可重新打开 AE 确认入口已移除。\n日志：" + log.wstring();
            try { append_install_log(log, L"卸载成功：" + targets + L" 文件数：" + std::to_wstring(removed)); } catch (...) {}
            if (!options.silent) MessageBoxW(nullptr, message.c_str(), L"效果控制台卸载完成", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
#ifndef XFX_UNINSTALLER
        install_files(roots, {read_resource(IDR_XFX_PLUGIN), read_resource(IDR_XFX_ALIASES),
            read_resource(IDR_XFX_NOTICES), read_resource(IDR_XFX_PANEL)});
#else
        throw std::runtime_error("请使用安装器执行安装。");
#endif
        const std::wstring message = L"效果控制台安装完成。\n\n安装目录：" + targets +
            L"\n\n重新打开 AE 后，按 Ctrl+空格使用。\n也可从“窗口”菜单打开“效果控制台”工具栏。\n日志：" + log.wstring();
        try { append_install_log(log, L"安装成功：" + targets); } catch (...) {}
        if (!options.silent) MessageBoxW(nullptr, message.c_str(), L"效果控制台安装成功", MB_OK | MB_ICONINFORMATION);
        return 0;
    } catch (const std::exception& error) {
        const std::wstring detail = wide_text(error.what());
        try { append_install_log(log, L"安装失败：" + detail); } catch (...) {}
        std::wcerr << detail << std::endl;
        if (!options.silent) MessageBoxW(nullptr, (L"操作没有完成。\n\n" + detail + L"\n\n日志：" + log.wstring()).c_str(),
                                         L"效果控制台：请检查后重试", MB_OK | MB_ICONWARNING);
        return 1;
    }
}
#endif
