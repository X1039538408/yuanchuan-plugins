#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include "ui_theme.h"

// ABI facts verified against Adobe's SoSharedLibDefs.h and Tools Guide CC p214.
// Kept independent of the Adobe sample implementation.
#pragma pack(push, 8)
struct XfxTaggedData {
    union { long intval; double fltval; char* string; long* hObject; } data;
    long type;
    long filler;
};
#pragma pack(pop)
static_assert(sizeof(long) == 4 && sizeof(XfxTaggedData) == 16 && alignof(XfxTaggedData) == 8);
constexpr long kXfxTypeUndefined = 0, kXfxTypeBool = 2, kXfxTypeDouble = 3,
    kXfxTypeString = 4, kXfxTypeInteger = 123, kXfxTypeUInteger = 124;

namespace xfx {
constexpr long kBridgeVersion = 10003;
enum class BridgeCommand { Show, Capture, Gallery, Export, Preferences, Shortcut, About };
struct BridgeRequest { std::uint64_t id; BridgeCommand command; int slot = -1; };
struct BridgeCompletion {
    bool succeeded = false;
    unsigned int count = 0;
    std::string code;
    std::string message;
};
class ScriptBridge {
public:
    using Clock = std::chrono::steady_clock;
    explicit ScriptBridge(std::function<Clock::time_point()> now = Clock::now);
    void set_available(bool available);
    void set_light_theme(bool light_theme);
    void set_theme(const ThemeColors& colors);
    std::string appearance() const;
    std::string status(std::optional<std::uint64_t> id = std::nullopt) const;
    std::string submit(BridgeCommand command, int slot = -1);
    std::string cancel(std::uint64_t id);
    std::optional<BridgeRequest> take_request();
    bool complete(std::uint64_t id, BridgeCompletion result);
private:
    enum class State { Queued, Running, Complete };
    struct Record { BridgeRequest request; Clock::time_point queued_at; State state; BridgeCompletion result; };
    std::string describe(const Record& record) const;
    void trim_completed();
    mutable std::mutex mutex_;
    std::function<Clock::time_point()> now_;
    bool available_ = false;
    bool light_theme_ = false;
    std::optional<ThemeColors> palette_;
    std::uint64_t next_id_ = 1;
    std::deque<std::uint64_t> queue_;
    std::map<std::uint64_t, Record> records_;
};
ScriptBridge& script_bridge();
}

#ifndef XFX_ES_EXPORT
#define XFX_ES_EXPORT extern "C" __declspec(dllexport)
#endif
XFX_ES_EXPORT long ESGetVersion();
XFX_ES_EXPORT char* ESInitialize(XfxTaggedData* argv, long argc);
XFX_ES_EXPORT void ESFreeMem(void* memory);
XFX_ES_EXPORT void ESTerminate();
XFX_ES_EXPORT long execute(XfxTaggedData* argv, long argc, XfxTaggedData* result);
