#define XFX_INSTALLER_TEST
#include "../XFXNativeConsoleInstaller.cpp"
#include <iostream>
#include <map>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void put(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << contents;
}
static std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}
static std::map<std::wstring, std::string> tree_snapshot(const fs::path& root) {
    std::map<std::wstring, std::string> snapshot;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const std::wstring relative = fs::relative(entry.path(), root).wstring();
        if (entry.is_directory()) snapshot[L"D:" + relative] = {};
        else if (entry.is_regular_file()) snapshot[L"F:" + relative] = read(entry.path());
    }
    return snapshot;
}
static fs::path ae_fixture(const fs::path& root, const wchar_t* name) {
    const fs::path target = root / name / L"Support Files";
    fs::create_directories(target / L"Plug-ins");
    put(target / L"AfterFX.exe", "fixture");
    return target;
}
int main() {
    const fs::path root = fs::temp_directory_path() / (L"xfx-native-installer-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        fs::create_directories(root);
        require(is_after_effects_process_name(L"AfterFX.exe"), "AE exe was not recognized");
        require(is_after_effects_process_name(L"aFtErFx.CoM"), "AE console host was not recognized case-insensitively");
        require(is_after_effects_process_name(L"AFTERFXBETA.COM"), "AE beta console host was not recognized");
        require(is_after_effects_process_name(L"AfterFXBeta.exe"), "AE beta exe was not recognized");
        require(!is_after_effects_process_name(L"AfterFX.exe.old") && !is_after_effects_process_name(L"MyAfterFX.exe") &&
                !is_after_effects_process_name(L"AfterFX.com-helper") && !is_after_effects_process_name(L""), "unrelated process matched AE");
        std::cout << "PASS exact AE executable names, console hosts and case handling\n";
        const install_payload payload{{'n','e','w'}, {'d','e','f','a','u','l','t'}, {'M','I','T'}, {'p','a','n','e','l'}};
        {
            // Only this copied PING process is owned by the test; never close a real AE.
            struct owned_probe {
                PROCESS_INFORMATION process{};
                ~owned_probe() {
                    if (process.hProcess) { TerminateProcess(process.hProcess, 0); WaitForSingleObject(process.hProcess, 5000); CloseHandle(process.hProcess); }
                    if (process.hThread) CloseHandle(process.hThread);
                }
            } child;
            const auto executable = root / L"AfterFX.exe";
            wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
            fs::copy_file(fs::path(system) / L"PING.EXE", executable, fs::copy_options::overwrite_existing);
            std::wstring command = L"\"" + executable.wstring() + L"\" -t 127.0.0.1";
            STARTUPINFOW startup{sizeof(startup)};
            require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &child.process) != FALSE, "failed to create non-destructive AE probe");
            const auto blocked = ae_fixture(root, L"running-ae");
            const auto before = tree_snapshot(blocked);
            bool rejected = false; std::string detail;
            try { install_files({blocked}, payload); } catch (const std::exception& e) { rejected = true; detail = e.what(); }
            require(WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT, "installer terminated the running AE probe");
            require(rejected && tree_snapshot(blocked) == before, "running AE must block install without changing files");
            require(detail.find("保存") != std::string::npos && detail.find("退出") != std::string::npos,
                "running AE message must instruct save and exit");
            rejected = false;
            try { uninstall_files({blocked}); } catch (const std::exception&) { rejected = true; }
            require(rejected && tree_snapshot(blocked) == before && WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT,
                "running AE must block uninstall without closing it or changing files");
        }
        std::cout << "PASS running AE blocks installation without closing it or changing targets\n";
        const auto fresh = ae_fixture(root, L"fresh");
        install_files({fresh}, payload);
        require(read(fresh / L"Plug-ins/XFXNativeConsole.aex") == "new", "fresh plugin missing");
        require(read(fresh / L"Plug-ins/XFXNativeConsole.aliases.default.tsv") == "default", "default dictionary missing");
        require(!fs::exists(fresh / L"Plug-ins/XFXNativeConsole.aliases.tsv"), "fresh install created legacy dictionary");
        require(read(fresh / L"Plug-ins/XFXNativeConsole.THIRD-PARTY-NOTICES.txt") == "MIT", "third-party notices missing");
        require(read(fresh / L"Scripts/ScriptUI Panels/效果控制台.jsx") == "panel", "Chinese dockable panel missing");
        std::cout << "PASS native fresh install\n";

        const auto upgrade = ae_fixture(root, L"upgrade");
        put(upgrade / L"Plug-ins/XFXNativeConsole.aex", "old");
        put(upgrade / L"Plug-ins/XFXNativeConsole.aliases.tsv", "custom");
        put(upgrade / L"Plug-ins/XFXNativeConsole.aliases.default.tsv", "old-default");
        const auto bridge = upgrade / L"Scripts/ScriptUI Panels/XFXEnhancerBridge.jsx";
        put(bridge, "bridge");
        const auto panel = upgrade / L"Scripts/ScriptUI Panels/效果控制台.jsx";
        const auto originalPanel = upgrade / L"Scripts/ScriptUI Panels/FX Console.jsx";
        put(panel, "old-panel"); put(originalPanel, "original-vendor-panel");
        install_files({upgrade}, payload);
        require(read(upgrade / L"Plug-ins/XFXNativeConsole.aliases.tsv") == "custom", "custom aliases overwritten");
        require(!fs::exists(bridge), "old bridge still enabled");
        bool backed_up = false;
        for (const auto& entry : fs::directory_iterator(bridge.parent_path())) {
            if (entry.path().filename().wstring().find(L".disabled-") != std::wstring::npos)
                backed_up = read(entry.path()) == "bridge";
        }
        require(backed_up, "bridge original was not preserved");
        require(read(panel) == "panel" && read(originalPanel) == "original-vendor-panel", "panel upgrade changed the vendor file");
        size_t panel_backups = 0;
        for (const auto& entry : fs::directory_iterator(panel.parent_path())) {
            if (entry.path().filename().wstring().rfind(L"效果控制台.jsx.backup-", 0) == 0) {
                ++panel_backups;
                require(read(entry.path()) == "old-panel", "Chinese panel backup lost old content");
            }
        }
        require(panel_backups == 1, "upgrade must retain exactly one backup of the old Chinese panel");
        std::cout << "PASS native upgrade preserves legacy dictionary and bridge\n";

        const auto rollback = ae_fixture(root, L"rollback");
        const auto plugin = rollback / L"Plug-ins/XFXNativeConsole.aex";
        const auto dictionary = rollback / L"Plug-ins/XFXNativeConsole.aliases.default.tsv";
        put(plugin, "old-plugin");
        put(dictionary, "old-default");
        const auto rollback_before = tree_snapshot(rollback);
        HANDLE locked = CreateFileW(dictionary.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "failed to lock fixture");
        bool rejected = false;
        try { install_files({rollback}, payload); } catch (const std::exception&) { rejected = true; }
        CloseHandle(locked);
        require(rejected, "locked file must fail installation");
        require(read(plugin) == "old-plugin", "rollback did not restore plugin");
        require(read(dictionary) == "old-default", "rollback changed dictionary");
        require(tree_snapshot(rollback) == rollback_before, "rollback left backup/stage files or newly created Scripts directories");
        for (const auto& entry : fs::directory_iterator(plugin.parent_path()))
            require(entry.path().filename().wstring().find(L".stage-") == std::wstring::npos, "rollback left stage file");
        std::cout << "PASS native rollback after locked dictionary\n";

        const auto panelRollback = ae_fixture(root, L"panel-rollback");
        const auto lockedPanel = panelRollback / L"Scripts/ScriptUI Panels/效果控制台.jsx";
        put(panelRollback / L"Plug-ins/XFXNativeConsole.aex", "old-plugin");
        put(panelRollback / L"Plug-ins/XFXNativeConsole.aliases.default.tsv", "old-default");
        put(panelRollback / L"Plug-ins/XFXNativeConsole.THIRD-PARTY-NOTICES.txt", "old-notices");
        put(panelRollback / L"Plug-ins/XFXNativeConsole.aliases.tsv", "custom-legacy");
        put(lockedPanel, "old-panel");
        put(lockedPanel.parent_path() / L"FX Console.jsx", "original-vendor-panel");
        put(lockedPanel.parent_path() / L"XFXEnhancerBridge.jsx", "old-bridge");
        const auto panel_before = tree_snapshot(panelRollback);
        locked = CreateFileW(lockedPanel.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "failed to lock panel fixture");
        rejected = false;
        try { install_files({panelRollback}, payload); } catch (const std::exception&) { rejected = true; }
        CloseHandle(locked);
        require(rejected && read(lockedPanel) == "old-panel", "locked panel was not preserved");
        require(read(panelRollback / L"Plug-ins/XFXNativeConsole.aex") == "old-plugin" &&
            read(panelRollback / L"Plug-ins/XFXNativeConsole.aliases.default.tsv") == "old-default", "panel failure did not roll back native files");
        require(tree_snapshot(panelRollback) == panel_before, "locked panel failure must restore every original file and remove transaction debris");
        std::cout << "PASS native panel failure rolls back all native files\n";

        const auto missingPanel = ae_fixture(root, L"missing-panel-payload");
        put(missingPanel / L"Plug-ins/XFXNativeConsole.aex", "old-plugin");
        const auto missing_before = tree_snapshot(missingPanel);
        auto incomplete_payload = payload;
        incomplete_payload.panel.clear();
        rejected = false;
        try { install_files({missingPanel}, incomplete_payload); } catch (const std::exception&) { rejected = true; }
        require(rejected && tree_snapshot(missingPanel) == missing_before, "missing panel payload mutated the target");
        std::cout << "PASS native missing panel payload changes no target files\n";

        const auto createsScripts = ae_fixture(root, L"created-scripts-rollback");
        const auto blockedScripts = ae_fixture(root, L"blocked-scripts-rollback");
        put(blockedScripts / L"Scripts", "user-file-not-directory");
        const auto creates_before = tree_snapshot(createsScripts);
        const auto blocked_before = tree_snapshot(blockedScripts);
        rejected = false;
        try { install_files({createsScripts, blockedScripts}, payload); } catch (const std::exception&) { rejected = true; }
        require(rejected && tree_snapshot(createsScripts) == creates_before && tree_snapshot(blockedScripts) == blocked_before,
                "directory creation failure must remove only newly created empty Scripts directories");
        std::cout << "PASS native directory setup failure cleans newly created Scripts directories\n";

        const auto invalid = root / L"not-ae";
        fs::create_directories(invalid);
        rejected = false;
        try { install_files({invalid}, payload); } catch (const std::exception&) { rejected = true; }
        require(rejected && !fs::exists(invalid / L"Plug-ins"), "invalid AE directory changed");
        std::cout << "PASS native rejects non-AE directory\n";

        const auto first = ae_fixture(root, L"AE2024");
        const auto second = ae_fixture(root, L"AE2026");
        install_files({first, second}, payload);
        require(read(first / L"Plug-ins/XFXNativeConsole.aex") == "new" &&
                read(second / L"Plug-ins/XFXNativeConsole.aex") == "new", "multi-target installation failed");
        auto options = parse_options({L"--target", first.wstring(), L"--silent", L"--log", (root / L"install.log").wstring()});
        require(options.silent && options.target == first && options.log == root / L"install.log", "CLI options failed");
        std::vector<fs::path> discovered;
        add_candidate(discovered, first / L"Plug-ins");
        add_candidate(discovered, second.parent_path());
        add_candidate(discovered, first);
        add_candidate(discovered, invalid);
        require(discovered.size() == 2, "discovery must validate and deduplicate multiple AE versions");
        std::cout << "PASS native multiple targets and CLI parsing\n";

        const auto transactionFirst = ae_fixture(root, L"transaction-first");
        const auto transactionSecond = ae_fixture(root, L"transaction-second");
        const auto secondDefaults = transactionSecond / L"Plug-ins/XFXNativeConsole.aliases.default.tsv";
        put(transactionFirst / L"Plug-ins/XFXNativeConsole.aex", "first-old");
        put(transactionSecond / L"Plug-ins/XFXNativeConsole.aex", "second-old");
        put(secondDefaults, "second-defaults");
        const auto transaction_first_before = tree_snapshot(transactionFirst);
        const auto transaction_second_before = tree_snapshot(transactionSecond);
        locked = CreateFileW(secondDefaults.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "failed to lock second version fixture");
        rejected = false;
        try { install_files({transactionFirst, transactionSecond}, payload); } catch (const std::exception&) { rejected = true; }
        CloseHandle(locked);
        require(rejected, "locked second version must fail installation");
        require(read(transactionFirst / L"Plug-ins/XFXNativeConsole.aex") == "first-old", "first version was not rolled back");
        require(!fs::exists(transactionFirst / L"Plug-ins/XFXNativeConsole.aliases.default.tsv"), "rollback retained newly created dictionary");
        require(read(transactionSecond / L"Plug-ins/XFXNativeConsole.aex") == "second-old", "second version was not rolled back");
        require(tree_snapshot(transactionFirst) == transaction_first_before && tree_snapshot(transactionSecond) == transaction_second_before,
                "multi-version rollback left panel files, backup files or newly created directories");
        std::cout << "PASS native rollback spans all selected versions\n";
        const auto uninstallFirst = ae_fixture(root, L"uninstall-first");
        const auto uninstallSecond = ae_fixture(root, L"uninstall-second");
        for (const auto& target : {uninstallFirst, uninstallSecond}) {
            install_files({target}, payload);
            put(target / L"Plug-ins/XFXNativeConsole.aliases.tsv", "user-aliases");
            put(target / L"Plug-ins/OtherVendor.aex", "third-party");
            put(target / L"Plug-ins/XFXNativeConsole.aex.backup-old", "retained-backup");
            put(target / L"Scripts/ScriptUI Panels/XFX 中文控制台.jsx", "legacy-panel");
            put(target / L"Scripts/ScriptUI Panels/FX Console.jsx", "third-party-panel");
        }
        const auto uninstallLocked = uninstallSecond / L"Plug-ins/XFXNativeConsole.aliases.default.tsv";
        const auto uninstallBeforeFirst = tree_snapshot(uninstallFirst), uninstallBeforeSecond = tree_snapshot(uninstallSecond);
        locked = CreateFileW(uninstallLocked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "failed to lock uninstall fixture");
        rejected = false;
        try { uninstall_files({uninstallFirst, uninstallSecond}); } catch (const std::exception&) { rejected = true; }
        CloseHandle(locked);
        require(rejected && tree_snapshot(uninstallFirst) == uninstallBeforeFirst && tree_snapshot(uninstallSecond) == uninstallBeforeSecond,
            "locked uninstall must roll back all selected versions");
        const size_t removed = uninstall_files({uninstallFirst, uninstallSecond});
        require(removed == 10, "uninstall must remove only five named active files per version");
        for (const auto& target : {uninstallFirst, uninstallSecond}) {
            require(!fs::exists(target / L"Plug-ins/XFXNativeConsole.aex"), "uninstaller left plugin active");
            require(!fs::exists(target / L"Scripts/ScriptUI Panels/效果控制台.jsx") &&
                !fs::exists(target / L"Scripts/ScriptUI Panels/XFX 中文控制台.jsx"), "uninstaller left toolbar active");
            require(read(target / L"Plug-ins/XFXNativeConsole.aliases.tsv") == "user-aliases" &&
                read(target / L"Plug-ins/OtherVendor.aex") == "third-party" &&
                read(target / L"Plug-ins/XFXNativeConsole.aex.backup-old") == "retained-backup" &&
                read(target / L"Scripts/ScriptUI Panels/FX Console.jsx") == "third-party-panel", "uninstaller removed protected user/vendor/backup files");
        }
        require(uninstall_files({uninstallFirst, uninstallSecond}) == 0, "repeat uninstall must be harmless");
        rejected = false;
        try { uninstall_files({invalid}); } catch (const std::exception&) { rejected = true; }
        require(rejected, "uninstaller accepted non-AE root");
        auto uninstallOptions = parse_options({L"--uninstall", L"--silent", L"--target", uninstallFirst.wstring()});
        require(uninstallOptions.uninstall && uninstallOptions.silent, "uninstall CLI parsing failed");
        std::cout << "PASS uninstall exact allowlist, both toolbar names, preserved data, multi-target rollback, repeat and CLI\n";
        fs::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        std::cerr << "Test fixtures retained at " << root.u8string() << '\n';
        return 1;
    }
}
