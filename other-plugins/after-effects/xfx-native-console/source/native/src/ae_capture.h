#pragma once
#include "AEConfig.h"
#include "AE_GeneralPlug.h"
#include "console_support.h"

namespace xfx {

struct CaptureCompName { ScriptReply reply; std::wstring name; };
// Read-only metadata query on AE's UI thread. Uses the same active-layer/comp
// selection as capture_comp_png, without acquiring render resources.
CaptureCompName current_capture_comp_name(SPBasicSuite* basic, AEGP_PluginID plugin_id);

// Call on AE's UI thread in response to an explicit capture request. Captures a
// comp into an atomic PNG without changing project/comp properties. false keeps
// NewFromItem's default downsample; true requests 1x1. Final width corrects PAR.
// Uses project depth and active-layer CompTime (otherwise item time). Applies
// the MRU view color transform to an owned world and restores source alpha.
// Uses explicit straight alpha/ARGB and owns all acquired SDK resources.
// The target directory must already exist. No GDI+ initialization is required.
ScriptReply capture_comp_png(SPBasicSuite* basic, AEGP_PluginID plugin_id,
                             const std::filesystem::path& target, bool full_resolution);

} // namespace xfx
