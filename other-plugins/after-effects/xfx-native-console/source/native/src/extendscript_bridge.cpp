#include "extendscript_bridge.h"
#include "ui_theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <new>
#include <cstdio>

namespace {
constexpr std::uint64_t kMaxScriptInteger = 9007199254740991ULL;
std::string field(std::string text, size_t limit = 4096) {
    for (char& character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 32 || byte == 127) character = ' ';
    }
    if (text.size() > limit) {
        while (limit && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80) --limit;
        text.resize(limit);
    }
    return text;
}
std::string error(std::uint64_t id, const std::string& code, const std::string& message) {
    return "XFX1\tERROR\t" + std::to_string(id) + "\t" + field(code, 64) + "\t" + field(message);
}
bool integer_argument(const XfxTaggedData& value, std::uint64_t& number) {
    if (value.type == kXfxTypeInteger) {
        if (value.data.intval < 0) return false;
        number = static_cast<std::uint64_t>(value.data.intval);
        return true;
    }
    if (value.type == kXfxTypeUInteger) {
        number = static_cast<std::uint32_t>(value.data.intval); return true;
    }
    if (value.type != kXfxTypeDouble || !std::isfinite(value.data.fltval) || value.data.fltval < 0 ||
        value.data.fltval > static_cast<double>(kMaxScriptInteger) ||
        std::floor(value.data.fltval) != value.data.fltval) return false;
    number = static_cast<std::uint64_t>(value.data.fltval); return true;
}
}

namespace xfx {
ScriptBridge::ScriptBridge(std::function<Clock::time_point()> now) : now_(std::move(now)) {}
void ScriptBridge::set_light_theme(bool light_theme) { std::lock_guard lock(mutex_); light_theme_ = light_theme; palette_.reset(); }
void ScriptBridge::set_theme(const ThemeColors& colors) {
    std::lock_guard lock(mutex_); palette_ = colors; light_theme_ = is_light_background(colors.background);
}
std::string ScriptBridge::appearance() const {
    std::lock_guard lock(mutex_);
    if (!available_) return error(0, "unavailable", "原生插件尚未就绪，请检查安装后重新打开 AE");
    const auto& palette = palette_ ? *palette_ : theme_colors(light_theme_);
    const auto rgb = [](COLORREF color) {
        char result[7]{};
        std::snprintf(result, sizeof(result), "%02X%02X%02X", static_cast<unsigned>(GetRValue(color)),
            static_cast<unsigned>(GetGValue(color)), static_cast<unsigned>(GetBValue(color)));
        return std::string(result);
    };
    return std::string("XFX1\tTHEME\t") + (light_theme_ ? "light\t" : "dark\t") +
        rgb(palette.background) + "\t" + rgb(palette.icon) + "\t" + rgb(palette.selected) + "\t" + rgb(palette.button_pressed);
}
void ScriptBridge::set_available(bool available) {
    std::lock_guard lock(mutex_);
    available_ = available;
    if (!available) {
        queue_.clear();
        for (auto& [id, record] : records_) {
            if (record.state == State::Complete) continue;
            record.result = {false, 0, "unavailable", "AE 插件已关闭，操作未完成"};
            record.state = State::Complete;
        }
    }
}
std::string ScriptBridge::describe(const Record& record) const {
    const auto id = std::to_string(record.request.id);
    if (record.state != State::Complete) return "XFX1\tPENDING\t" + id;
    if (!record.result.succeeded) return error(record.request.id,
        record.result.code.empty() ? "failed" : record.result.code, record.result.message);
    return "XFX1\tOK\t" + id + "\t" + std::to_string(record.result.count) + "\t" + field(record.result.message);
}
std::string ScriptBridge::status(std::optional<std::uint64_t> id) const {
    std::lock_guard lock(mutex_);
    if (!id) return available_ ? "XFX1\tREADY\t" + std::to_string(kBridgeVersion)
        : error(0, "unavailable", "原生插件尚未就绪，请检查安装后重新打开 AE");
    const auto found = records_.find(*id);
    return found == records_.end() ? error(*id, "unknown_request", "请求不存在或结果已过期") : describe(found->second);
}
void ScriptBridge::trim_completed() {
    while (records_.size() >= 64) {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [](const auto& entry) { return entry.second.state == State::Complete; });
        if (found == records_.end()) break;
        records_.erase(found);
    }
}
std::string ScriptBridge::submit(BridgeCommand command, int slot) {
    std::lock_guard lock(mutex_);
    if (!available_) return error(0, "unavailable", "原生插件尚未就绪，请检查安装后重新打开 AE");
    if (command < BridgeCommand::Show || command > BridgeCommand::About ||
        (command == BridgeCommand::Shortcut ? slot < 0 || slot > 8 : slot != -1)) {
        return error(0, "invalid_argument", "命令参数不正确");
    }
    const auto outstanding = std::count_if(records_.begin(), records_.end(),
        [](const auto& entry) { return entry.second.state != State::Complete; });
    if (outstanding >= 32) return error(0, "busy", "待处理操作过多，请稍后重试");
    if (next_id_ > kMaxScriptInteger) return error(0, "unavailable", "请求编号已用尽，请重新打开 AE");
    const auto id = next_id_++;
    auto response = "XFX1\tQUEUED\t" + std::to_string(id);
    trim_completed();
    records_.emplace(id, Record{{id, command, slot}, now_(), State::Queued, {}});
    try { queue_.push_back(id); }
    catch (...) { records_.erase(id); throw; }
    return response;
}
std::string ScriptBridge::cancel(std::uint64_t id) {
    std::lock_guard lock(mutex_);
    const auto found = records_.find(id);
    if (found == records_.end()) return error(id, "unknown_request", "请求不存在或结果已过期");
    auto& record = found->second;
    if (record.state == State::Running) return error(id, "running", "AE 正在处理此操作");
    if (record.state == State::Queued) {
        record.result = {false, 0, "cancelled", "操作已取消"};
        record.state = State::Complete;
        queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    }
    return describe(record);
}
std::optional<BridgeRequest> ScriptBridge::take_request() {
    std::lock_guard lock(mutex_);
    if (!available_) return std::nullopt;
    while (!queue_.empty()) {
        const auto id = queue_.front(); queue_.pop_front();
        auto& record = records_.at(id);
        if (now_() - record.queued_at > std::chrono::seconds(30)) {
            record.result = {false, 0, "expired", "AE 长时间忙碌，此操作已取消，请重试"};
            record.state = State::Complete;
            continue;
        }
        record.state = State::Running;
        return record.request;
    }
    return std::nullopt;
}
bool ScriptBridge::complete(std::uint64_t id, BridgeCompletion result) {
    std::lock_guard lock(mutex_);
    const auto found = records_.find(id);
    if (found == records_.end() || found->second.state != State::Running) return false;
    found->second.result = std::move(result);
    found->second.state = State::Complete;
    return true;
}
ScriptBridge& script_bridge() { static ScriptBridge bridge; return bridge; }
}
long ESGetVersion() { return xfx::kBridgeVersion; }
char* ESInitialize(XfxTaggedData*, long) { static char signatures[] = "execute"; return signatures; }
void ESFreeMem(void* memory) { std::free(memory); }
// The AE entry point owns host state; unloading a ScriptUI client must leave it intact.
void ESTerminate() {}
long execute(XfxTaggedData* argv, long argc, XfxTaggedData* result) {
    if (!result) return 20;
    *result = {};
    if (!argv || argc < 1 || argc > 2 || argv[0].type != kXfxTypeString || !argv[0].data.string) return 20;
    const auto length = strnlen_s(argv[0].data.string, 33);
    if (!length || length > 32) return 20;
    std::uint64_t argument = 0;
    if (argc == 2 && !integer_argument(argv[1], argument)) return 20;
    try {
        // Allocate returned storage before accepting an action into the queue.
        std::unique_ptr<char, decltype(&std::free)> memory(static_cast<char*>(std::malloc(8192)), &std::free);
        if (!memory) return -28;
        const std::string command(argv[0].data.string, length);
        auto& bridge = xfx::script_bridge();
        std::string response;
        if (command == "motion") {
            if (argc != 1) return 20;
            BOOL animation = TRUE;
            SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
            response = animation ? "XFX1\tMOTION\t0" : "XFX1\tMOTION\t1";
        } else if (command == "appearance") {
            if (argc != 1) return 20;
            response = bridge.appearance();
        } else if (command == "requeststatus") {
            if (argc == 2 && !argument) return 20;
            response = bridge.status(argc == 2 ? std::optional<std::uint64_t>(argument) : std::nullopt);
        } else if (command == "cancel") {
            if (argc != 2 || !argument) return 20;
            response = bridge.cancel(argument);
        } else {
            std::optional<xfx::BridgeCommand> action;
            using Command = xfx::BridgeCommand;
            if (command == "show") action = Command::Show;
            if (command == "screenshot") action = Command::Capture;
            if (command == "gallery") action = Command::Gallery;
            if (command == "export") action = Command::Export;
            if (command == "preferences") action = Command::Preferences;
            if (command == "shortcut") action = Command::Shortcut;
            // The original ScriptUI panel calls this action "logo". Keep
            // "about" as a compatibility alias for older local panels.
            if (command == "logo" || command == "about") action = Command::About;
            if (!action) response = error(0, "unknown_command", "不支持此操作，请更新配套面板");
            else {
                const bool shortcut = *action == Command::Shortcut;
                if ((shortcut && (argc != 2 || argument > 8)) || (!shortcut && argc != 1)) return 20;
                response = bridge.submit(*action, shortcut ? static_cast<int>(argument) : -1);
            }
        }
        if (response.size() >= 8192) return 32;
        std::memcpy(memory.get(), response.c_str(), response.size() + 1);
        result->data.string = memory.release(); result->type = kXfxTypeString;
        return 0;
    } catch (const std::bad_alloc&) { return -28; }
    catch (...) { return 32; }
}
