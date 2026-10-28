#define XFX_ES_EXPORT extern "C"
#include "../src/extendscript_bridge.h"
#include <windows.h>
#include <cassert>
#include <cstring>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    assert(argc == 2);
    const HMODULE module = LoadLibraryW(argv[1]);
    if (!module) { std::cerr << "FAIL LoadLibrary: " << GetLastError() << '\n'; return 1; }
    const auto version = reinterpret_cast<long(*)()>(GetProcAddress(module, "ESGetVersion"));
    const auto initialize = reinterpret_cast<char*(*)(XfxTaggedData*, long)>(GetProcAddress(module, "ESInitialize"));
    const auto free_memory = reinterpret_cast<void(*)(void*)>(GetProcAddress(module, "ESFreeMem"));
    const auto terminate = reinterpret_cast<void(*)()>(GetProcAddress(module, "ESTerminate"));
    const auto invoke = reinterpret_cast<long(*)(XfxTaggedData*, long, XfxTaggedData*)>(GetProcAddress(module, "execute"));
    if (!version || !initialize || !free_memory || !terminate || !invoke || !GetProcAddress(module, "EntryPointFunc")) {
        std::cerr << "FAIL required AEGP/ExtendScript export missing\n"; FreeLibrary(module); return 1;
    }
    assert(version() == xfx::kBridgeVersion);
    assert(std::strcmp(initialize(nullptr, 0), "execute") == 0);
    for (const char* command : {"requeststatus", "appearance", "show", "screenshot", "gallery", "export", "preferences", "logo", "about"}) {
        XfxTaggedData argument{}, result{};
        argument.type = kXfxTypeString; argument.data.string = const_cast<char*>(command);
        assert(invoke(&argument, 1, &result) == 0);
        assert(result.type == kXfxTypeString && result.filler == 0 && result.data.string);
        const std::string reply(result.data.string);
        assert(reply.starts_with("XFX1\tERROR\t0\tunavailable\t"));
        assert(reply.find("原生插件") != std::string::npos);
        free_memory(result.data.string);
    }
    terminate();
    FreeLibrary(module);
    std::cout << "PASS actual AEX exports, TaggedData ABI, version, UTF-8 and uninitialized-host errors\n";
}
