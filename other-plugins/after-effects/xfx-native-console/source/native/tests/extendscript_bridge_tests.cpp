#include "../src/extendscript_bridge.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

static std::uint64_t queued_id(const std::string& text) {
    const std::string prefix = "XFX1\tQUEUED\t";
    assert(text.starts_with(prefix));
    return std::stoull(text.substr(prefix.size()));
}
static XfxTaggedData text_arg(const char* text) {
    XfxTaggedData value{}; value.type = kXfxTypeString; value.data.string = const_cast<char*>(text); return value;
}
static XfxTaggedData number_arg(double number) {
    XfxTaggedData value{}; value.type = kXfxTypeDouble; value.data.fltval = number; return value;
}
static std::string call(const char* command, std::optional<double> number = std::nullopt) {
    XfxTaggedData args[] = {text_arg(command), number_arg(number.value_or(0))}, result{};
    assert(execute(args, number ? 2 : 1, &result) == 0);
    assert(result.type == kXfxTypeString && result.filler == 0 && result.data.string);
    std::string text(result.data.string); ESFreeMem(result.data.string); return text;
}
int main() {
    static_assert(offsetof(XfxTaggedData, type) == 8 && offsetof(XfxTaggedData, filler) == 12);
    xfx::ScriptBridge::Clock::time_point now{};
    xfx::ScriptBridge bridge([&] { return now; });
    assert(bridge.status().find("\tunavailable\t") != std::string::npos);
    assert(bridge.submit(xfx::BridgeCommand::Capture).find("\tunavailable\t") != std::string::npos);
    assert(!bridge.take_request());
    bridge.set_available(true);
    assert(bridge.status() == "XFX1\tREADY\t10003");
    const auto first = queued_id(bridge.submit(xfx::BridgeCommand::Capture));
    assert(bridge.status(first).starts_with("XFX1\tPENDING\t"));
    const auto request = bridge.take_request();
    assert(request && request->id == first && request->command == xfx::BridgeCommand::Capture);
    assert(!bridge.take_request());
    assert(bridge.complete(first, {true, 2, "", "中文快照\t已保存\r\n测试"}));
    assert(bridge.status(first) == "XFX1\tOK\t" + std::to_string(first) + "\t2\t中文快照 已保存  测试");
    assert(!bridge.complete(first, {false, 0, "fail", "must not overwrite"}));
    std::cout << "PASS host availability, real completion, UTF-8 and protocol delimiters\n";

    const auto cancelled = queued_id(bridge.submit(xfx::BridgeCommand::Gallery));
    assert(bridge.cancel(cancelled).find("\tcancelled\t") != std::string::npos);
    assert(!bridge.take_request());
    const auto running = queued_id(bridge.submit(xfx::BridgeCommand::Show));
    assert(bridge.take_request()->id == running);
    assert(bridge.cancel(running).find("\trunning\t") != std::string::npos);
    assert(bridge.complete(running, {false, 0, "host_error", "无合成"}));
    assert(bridge.status(running).find("\thost_error\t无合成") != std::string::npos);
    std::cout << "PASS cancel only queued requests and retain actual failures\n";

    const auto expired = queued_id(bridge.submit(xfx::BridgeCommand::Export));
    now += std::chrono::seconds(31);
    assert(!bridge.take_request());
    assert(bridge.status(expired).find("\texpired\t") != std::string::npos);
    const auto closed = queued_id(bridge.submit(xfx::BridgeCommand::Preferences));
    bridge.set_available(false);
    assert(bridge.status(closed).find("\tunavailable\t") != std::string::npos);
    bridge.set_available(true);
    assert(!bridge.take_request());
    std::cout << "PASS queue expiry and host shutdown prevent delayed actions\n";

    xfx::ScriptBridge bounded;
    bounded.set_available(true);
    for (int i = 0; i < 32; ++i) queued_id(bounded.submit(xfx::BridgeCommand::Show));
    assert(bounded.submit(xfx::BridgeCommand::Show).find("\tbusy\t") != std::string::npos);
    while (const auto next = bounded.take_request()) assert(bounded.complete(next->id, {true, 0, "", "ok"}));
    std::uint64_t old = 0;
    for (int i = 0; i < 100; ++i) {
        const auto id = queued_id(bounded.submit(xfx::BridgeCommand::Show));
        if (!i) old = id;
        assert(bounded.take_request()->id == id);
        assert(bounded.complete(id, {true, 0, "", "ok"}));
    }
    assert(bounded.status(old).find("\tunknown_request\t") != std::string::npos);
    std::cout << "PASS bounded requests and completed-result retention\n";

    assert(ESGetVersion() == xfx::kBridgeVersion);
    assert(std::string(ESInitialize(nullptr, 0)) == "execute");
    ESFreeMem(nullptr);
    auto& global = xfx::script_bridge();
    global.set_available(false);
    assert(call("requeststatus").find("\tunavailable\t") != std::string::npos);
    assert(call("appearance").find("\tunavailable\t") != std::string::npos);
    global.set_available(true);
    assert(call("appearance") == "XFX1\tTHEME\tdark\t202020\tC8C8C8\t146FCB\t444444");
    assert(!global.take_request()); // Theme queries are read-only and do not create host actions.
    global.set_light_theme(true);
    assert(call("appearance") == "XFX1\tTHEME\tlight\tEDEDED\t252525\t146FCB\tCFCFCF");
    global.set_light_theme(false);
    assert(!global.take_request());
    std::cout << "PASS read-only appearance uses shared palette and rejects an unavailable host\n";
    const auto id = queued_id(call("shortcut", 8));
    ESTerminate(); // unloading one ExternalObject must not reset the AE-owned service.
    auto action = global.take_request();
    assert(action && action->id == id && action->command == xfx::BridgeCommand::Shortcut && action->slot == 8);
    assert(global.complete(id, {true, 1, "", "完成"}));
    assert(call("requeststatus", static_cast<double>(id)).find("\t1\t完成") != std::string::npos);
    assert(call("unknown").find("\tunknown_command\t") != std::string::npos);
    std::cout << "PASS exported ABI, ownership, lifecycle and optional numeric arguments\n";

    XfxTaggedData bad = number_arg(1), result{};
    assert(execute(nullptr, 1, &result) == 20);
    assert(execute(&bad, 1, &result) == 20);
    assert(execute(&bad, 0, &result) == 20);
    assert(execute(&bad, 3, &result) == 20);
    assert(execute(&bad, 1, nullptr) == 20);
    XfxTaggedData bad_slot[] = {text_arg("shortcut"), number_arg(0.5)};
    for (double value : {-1.0, 0.5, 9.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bad_slot[1] = number_arg(value); assert(execute(bad_slot, 2, &result) == 20);
    }
    bad_slot[1].type = kXfxTypeBool; bad_slot[1].data.intval = 1;
    assert(execute(bad_slot, 2, &result) == 20);
    XfxTaggedData extra_theme_arg[] = {text_arg("appearance"), number_arg(0)};
    assert(execute(extra_theme_arg, 2, &result) == 20);
    assert(!global.take_request());
    std::cout << "PASS invalid ABI arguments never enqueue an action\n";

    xfx::ScriptBridge concurrent;
    concurrent.set_available(true);
    std::vector<std::thread> threads;
    std::vector<std::uint64_t> ids(16);
    for (size_t i = 0; i < ids.size(); ++i) threads.emplace_back([&, i] {
        ids[i] = queued_id(concurrent.submit(xfx::BridgeCommand::Gallery));
    });
    for (auto& thread : threads) thread.join();
    std::sort(ids.begin(), ids.end());
    assert(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    size_t consumed = 0;
    while (const auto next = concurrent.take_request()) {
        assert(std::binary_search(ids.begin(), ids.end(), next->id));
        assert(concurrent.complete(next->id, {true, 0, "", "ok"})); ++consumed;
    }
    assert(consumed == ids.size());
    std::cout << "PASS concurrent callers retain unique IDs and exactly one consumption\n";
}
