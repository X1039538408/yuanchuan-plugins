#pragma once

#include <string>

namespace xfx {

// Generate self-contained, ES3-compatible UTF-8 scripts for AEGP_ExecuteScript.
// Every script evaluates to OK\tN, ERROR\tmessage or PARTIAL\tN\tmessage.
// N counts completed layers (effect/preset) or completed items (capture/import).
// Errors are single-line text with protocol delimiters removed.
std::string script_apply_effect(const std::string& match_name);
std::string script_apply_layer_style(const std::string& match_name);
std::string script_apply_preset(const std::wstring& path);
std::string script_capture_png(const std::wstring& path);
std::string script_import_image(const std::wstring& path);

}  // namespace xfx
