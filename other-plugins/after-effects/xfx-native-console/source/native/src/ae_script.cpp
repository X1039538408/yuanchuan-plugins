#include "ae_script.h"

#include <cstdint>
#include <string_view>

namespace xfx {
namespace {

void append_hex_unit(std::string& out, std::uint32_t unit) {
    static const char hex[] = "0123456789abcdef";
    out += "\\u";
    for (int shift = 12; shift >= 0; shift -= 4) {
        out += hex[(unit >> shift) & 15];
    }
}

void append_js_character(std::string& out, std::uint32_t codepoint) {
    if (codepoint == '"' || codepoint == '\\') {
        out += '\\';
        out += static_cast<char>(codepoint);
    } else if (codepoint >= 0x20 && codepoint < 0x7f) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0xffff) {
        append_hex_unit(out, codepoint);
    } else if (codepoint <= 0x10ffff) {
        codepoint -= 0x10000;
        append_hex_unit(out, 0xd800 + (codepoint >> 10));
        append_hex_unit(out, 0xdc00 + (codepoint & 0x3ff));
    } else {
        append_hex_unit(out, 0xfffd);
    }
}

// Escaping UTF-16 code units also works with Windows surrogate pairs. Platforms
// with a 32-bit wchar_t use append_js_character's surrogate-pair conversion.
std::string js_string(const std::wstring& value) {
    std::string out = "\"";
    for (const wchar_t character : value) {
        append_js_character(out, static_cast<std::uint32_t>(character));
    }
    return out + '"';
}

std::string js_string(std::string_view value) {
    std::string out = "\"";
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i]);
        std::uint32_t codepoint = first;
        std::size_t length = 1;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            length = 2;
            minimum = 0x80;
            codepoint &= 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            length = 3;
            minimum = 0x800;
            codepoint &= 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            length = 4;
            minimum = 0x10000;
            codepoint &= 0x07;
        } else if (first >= 0x80) {
            append_js_character(out, 0xfffd);
            ++i;
            continue;
        }
        bool valid = length <= value.size() - i;
        for (std::size_t j = 1; valid && j < length; ++j) {
            const auto next = static_cast<unsigned char>(value[i + j]);
            valid = (next & 0xc0) == 0x80;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        valid = valid && codepoint >= minimum && codepoint <= 0x10ffff &&
                !(codepoint >= 0xd800 && codepoint <= 0xdfff);
        append_js_character(out, valid ? codepoint : 0xfffd);
        i += valid ? length : 1;
    }
    return out + '"';
}

std::string wrap_script(const std::string& body) {
    return R"JS((function () {
    var completed = 0;
    var problems = [];
    var undoOpen = false;
    function clean(value) {
        return String(value).replace(/[\x00-\x1f\u2028\u2029]/g, " ");
    }
    function problem(error, prefix) {
        var message = error && error.message ? error.message : String(error);
        problems.push(clean((prefix || "") + message));
    }
    function beginUndo(label) {
        app.beginUndoGroup(label);
        undoOpen = true;
    }
    function activeComp() {
        if (!app.project) { throw new Error("请先打开 AE 项目"); }
        var comp = app.project.activeItem;
        if (!(comp instanceof CompItem)) { throw new Error("请先激活一个合成"); }
        return comp;
    }
    function selectedLayers(comp) {
        var selected = comp.selectedLayers;
        var snapshot = [];
        for (var i = 0; i < selected.length; i++) { snapshot.push(selected[i]); }
        if (!snapshot.length) { throw new Error("请先选择一个或多个图层"); }
        return snapshot;
    }
    try {
)JS" + body + R"JS(
    } catch (error) {
        problem(error);
    } finally {
        if (undoOpen) {
            try { app.endUndoGroup(); }
            catch (undoError) { problem(undoError, "结束撤销组失败: "); }
        }
    }
    if (problems.length) {
        return completed > 0 ? "PARTIAL\t" + completed + "\t" + problems.join("；")
                             : "ERROR\t" + problems.join("；");
    }
    return "OK\t" + completed;
}());)JS";
}

}  // namespace

std::string script_apply_effect(const std::string& match_name) {
    return wrap_script("        var matchName = " + js_string(std::string_view(match_name)) + R"JS(;
        if (!matchName) { throw new Error("效果标识不能为空"); }
        var comp = activeComp();
        var layers = selectedLayers(comp);
        beginUndo("XFXConsole 应用效果");
        for (var i = 0; i < layers.length; i++) {
            var layer = layers[i];
            try {
                var effects = layer.property("ADBE Effect Parade");
                if (!effects || !effects.canAddProperty(matchName)) {
                    throw new Error("此图层不支持该效果");
                }
                if (!effects.addProperty(matchName)) {
                    throw new Error("AE 未能创建效果");
                }
                completed++;
            } catch (layerError) {
                problem(layerError, "图层 " + layer.name + ": ");
            }
        })JS");
}

std::string script_apply_preset(const std::wstring& path) {
    return wrap_script("        var path = " + js_string(path) + R"JS(;
        if (!path) { throw new Error("预设路径不能为空"); }
        var comp = activeComp();
        var layers = selectedLayers(comp);
        var preset = new File(path);
        if (!preset.exists) { throw new Error("找不到动画预设文件"); }
        var clearSelection = function () {
            for (var n = 1; n <= comp.numLayers; n++) { comp.layer(n).selected = false; }
        };
        beginUndo("XFXConsole 应用动画预设");
        try {
            for (var i = 0; i < layers.length; i++) {
                try {
                    clearSelection();
                    layers[i].selected = true;
                    layers[i].applyPreset(preset);
                    completed++;
                } catch (layerError) {
                    problem(layerError, "图层 " + layers[i].name + ": ");
                }
            }
        } finally {
            // Presets may change selection or create layers. Clear the current
            // comp selection, then restore the original layer references.
            for (var n = 1; n <= comp.numLayers; n++) {
                try { comp.layer(n).selected = false; }
                catch (clearError) { problem(clearError, "恢复图层选择失败: "); }
            }
            for (var j = 0; j < layers.length; j++) {
                try { layers[j].selected = true; }
                catch (restoreError) { problem(restoreError, "恢复图层选择失败: "); }
            }
        })JS");
}

std::string script_capture_png(const std::wstring& path) {
    return wrap_script("        var path = " + js_string(path) + R"JS(;
        if (!path) { throw new Error("PNG 保存路径不能为空"); }
        var comp = activeComp();
        var file = new File(path);
        if (!file.parent.exists) { throw new Error("PNG 保存文件夹不存在"); }
        if (typeof comp.saveFrameToPng !== "function") {
            throw new Error("当前 AE 版本不支持保存 PNG 帧");
        }
        comp.saveFrameToPng(comp.time, file);
        // ExtendScript may cache File.exists on the pre-render File object.
        // Refresh the object, and allow at most one second for filesystem visibility.
        var saved = new File(path).exists;
        for (var attempt = 0; !saved && attempt < 10; attempt++) {
            $.sleep(100);
            saved = new File(path).exists;
        }
        if (!saved) { throw new Error("AE 未能保存 PNG 帧，请检查写入权限"); }
        completed = 1;)JS");
}

std::string script_import_image(const std::wstring& path) {
    return wrap_script("        var path = " + js_string(path) + R"JS(;
        if (!path) { throw new Error("图片路径不能为空"); }
        if (!app.project) { throw new Error("请先打开 AE 项目"); }
        var file = new File(path);
        if (!file.exists) { throw new Error("找不到要导入的图片"); }
        var options = new ImportOptions(file);
        options.sequence = false;
        beginUndo("XFXConsole 导入图片");
        if (!app.project.importFile(options)) { throw new Error("AE 未能导入图片"); }
        completed = 1;)JS");
}

}  // namespace xfx
