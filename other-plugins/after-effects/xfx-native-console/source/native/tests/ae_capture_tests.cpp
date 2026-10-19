#include "ae_capture.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void check_hr(HRESULT value, const char* message) { require(SUCCEEDED(value), message); }
static constexpr AEGP_PluginID kPlugin = 71;
static constexpr A_Err kInjected = 731;
template<class T> static T fake_handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }

struct Options {
    AEGP_ItemH item{};
    A_Time time{0, 1};
    A_short down_x = 2, down_y = 2;
    AEGP_WorldType type = AEGP_WorldType_16;
    AEGP_MatteMode matte = AEGP_MatteMode_PREMUL_BLACK;
    AEGP_ChannelOrder order = AEGP_ChannelOrder_BGRA;
};
struct Receipt {
    A_long width = 0, height = 0;
    A_u_long stride = 0;
    AEGP_WorldType type = AEGP_WorldType_NONE;
    std::vector<std::uint8_t> pixels, before;
};
struct NameMemory {
    std::vector<A_UTF16Char> text;
    bool locked = false;
};
struct FakeHost;
static thread_local FakeHost* current = nullptr;

struct FakeHost {
    SPBasicSuite basic{};
    AEGP_ItemSuite8 item{}; AEGP_CompSuite7 comp{}; AEGP_LayerSuite8 layer{};
    AEGP_ProjSuite5 project_suite{}; AEGP_ColorSettingsSuite2 color{};
    AEGP_MemorySuite1 memory{};
    AEGP_RenderOptionsSuite3 options{}; AEGP_RenderSuite3 render{}; AEGP_WorldSuite3 world{};
    AEGP_ItemH primary = fake_handle<AEGP_ItemH>(0x1000), secondary = fake_handle<AEGP_ItemH>(0x2000);
    AEGP_CompH parent_comp = fake_handle<AEGP_CompH>(0x3000);
    AEGP_ProjectH project = fake_handle<AEGP_ProjectH>(0x5000);
    AEGP_ItemViewP view = fake_handle<AEGP_ItemViewP>(0x6000);
    AEGP_ProjBitDepth project_depth = AEGP_ProjBitDepth_8;
    AEGP_LayerH active_layer = nullptr;
    AEGP_ItemH active_item = primary;
    AEGP_ObjectType layer_type = AEGP_ObjectType_AV;
    AEGP_ItemType item_type = AEGP_ItemType_COMP;
    A_Time time{125, 25}, layer_time{77, 25}; A_Ratio par{1, 1};
    std::map<std::string, int> references, calls;
    std::set<Options*> live_options; std::set<Receipt*> live_receipts;
    std::set<Receipt*> live_worlds; std::set<AEGP_ColorProfileP> live_profiles;
    std::set<NameMemory*> live_names;
    std::wstring primary_name = L"当前合成_中文_\U0001F3AC", secondary_name = L"其他合成";
    bool null_name = false, null_name_pointer = false, unterminated_name = false, odd_name_size = false;
    std::vector<std::string> cleanup;
    std::string fail;
    bool partial_handle_on_error = false, null_options = false, null_receipt = false, null_world = false, null_base = false;
    bool wrong_world_type = false, invalid_size = false, short_stride = false, huge_size = false;
    bool source_modified = false;
    bool view_has_transform = false, null_profile = false, null_owned_world = false;
    A_long render_width = 6, render_height = 4;
    std::vector<std::array<A_u_char, 4>> byte_fixture;
    std::vector<std::array<A_u_short, 4>> word_fixture;
    std::vector<std::array<float, 4>> float_fixture;
    int option_downsample_calls = 0, project_mutations = 0, borrowed_world_disposals = 0;
    FakeHost();
    ~FakeHost() {
        for (auto* value : live_receipts) delete value;
        for (auto* value : live_options) delete value;
        for (auto* value : live_worlds) delete value;
        for (auto* value : live_profiles) delete reinterpret_cast<int*>(value);
        for (auto* value : live_names) delete value;
        current = nullptr;
    }
    A_Err step(const std::string& name) { ++calls[name]; return fail == name ? kInjected : A_Err_NONE; }
    void released() const {
        require(live_options.empty() && live_receipts.empty() && live_worlds.empty() && live_profiles.empty(),
            "AE options/receipt/world/profile leaked");
        require(live_names.empty(), "AE item name memory leaked");
        for (const auto& [name, count] : references) { (void)name; require(count == 0, "AE suite reference leaked"); }
        require(!source_modified && project_mutations == 0 && borrowed_world_disposals == 0,
            "capture changed project properties or the borrowed world");
    }
};
static Options* as_options(AEGP_RenderOptionsH value) { return reinterpret_cast<Options*>(value); }
static Receipt* as_world(AEGP_WorldH value) { return reinterpret_cast<Receipt*>(value); }

static SPErr SPAPI acquire(const char* name, int32 version, const void** suite) {
    auto& host = *current;
    const auto key = std::string(name) + ':' + std::to_string(version);
    if (host.step("acquire:" + key)) return kInjected;
    const void* table = nullptr;
    if (key == std::string(kAEGPItemSuite) + ':' + std::to_string(kAEGPItemSuiteVersion8)) table = &host.item;
    if (key == std::string(kAEGPCompSuite) + ':' + std::to_string(kAEGPCompSuiteVersion7)) table = &host.comp;
    if (key == std::string(kAEGPLayerSuite) + ':' + std::to_string(kAEGPLayerSuiteVersion8)) table = &host.layer;
    if (key == std::string(kAEGPRenderOptionsSuite) + ':' + std::to_string(kAEGPRenderOptionsSuiteVersion3)) table = &host.options;
    if (key == std::string(kAEGPRenderSuite) + ':' + std::to_string(kAEGPRenderSuiteVersion3)) table = &host.render;
    if (key == std::string(kAEGPWorldSuite) + ':' + std::to_string(kAEGPWorldSuiteVersion3)) table = &host.world;
    if (key == std::string(kAEGPProjSuite) + ':' + std::to_string(kAEGPProjSuiteVersion5)) table = &host.project_suite;
    if (key == std::string(kAEGPColorSettingsSuite) + ':' + std::to_string(kAEGPColorSettingsSuiteVersion2)) table = &host.color;
    if (key == std::string(kAEGPMemorySuite) + ':' + std::to_string(kAEGPMemorySuiteVersion1)) table = &host.memory;
    if (!table) return kInjected;
    *suite = table; ++host.references[key]; return kSPNoError;
}
static SPErr SPAPI release(const char* name, int32 version) {
    auto& host = *current;
    const auto key = std::string(name) + ':' + std::to_string(version);
    require(host.references[key] > 0, "released unacquired suite/version");
    --host.references[key]; host.cleanup.push_back("suite:" + key);
    return host.step("release:" + key);
}
static A_Err SPAPI get_active_layer(AEGP_LayerH* value) {
    if (const auto error = current->step("active-layer")) return error;
    *value = current->active_layer; return A_Err_NONE;
}
static A_Err SPAPI get_layer_type(AEGP_LayerH layer, AEGP_ObjectType* value) {
    require(layer == current->active_layer, "wrong active layer handle");
    if (const auto error = current->step("layer-type")) return error;
    *value = current->layer_type; return A_Err_NONE;
}
static A_Err SPAPI get_parent_comp(AEGP_LayerH layer, AEGP_CompH* value) {
    require(layer == current->active_layer, "wrong layer parent query");
    if (const auto error = current->step("parent-comp")) return error;
    *value = current->parent_comp; return A_Err_NONE;
}
static A_Err SPAPI get_item_from_comp(AEGP_CompH comp, AEGP_ItemH* value) {
    require(comp == current->parent_comp, "wrong comp item query");
    if (const auto error = current->step("comp-item")) return error;
    *value = current->primary; return A_Err_NONE;
}
static A_Err SPAPI get_comp_from_item(AEGP_ItemH item, AEGP_CompH* value) {
    require(item == current->primary || item == current->secondary, "wrong item comp query");
    if (const auto error = current->step("item-comp")) return error;
    *value = current->parent_comp; return A_Err_NONE;
}
static A_Err SPAPI get_project(A_long index, AEGP_ProjectH* value) {
    require(index == 0, "capture selected a different project index");
    if (const auto error = current->step("project")) return error;
    *value = current->project; return A_Err_NONE;
}
static A_Err SPAPI get_depth(AEGP_ProjectH project, AEGP_ProjBitDepth* value) {
    require(project && project == current->project, "wrong project depth query");
    if (const auto error = current->step("depth")) return error;
    *value = current->project_depth; return A_Err_NONE;
}
static A_Err SPAPI get_active_item(AEGP_ItemH* value) {
    if (const auto error = current->step("active-item")) return error;
    *value = current->active_item; return A_Err_NONE;
}
static A_Err SPAPI get_item_type(AEGP_ItemH value, AEGP_ItemType* type) {
    require(value == current->primary || value == current->secondary, "wrong item type query");
    if (const auto error = current->step("item-type")) return error;
    *type = current->item_type; return A_Err_NONE;
}
static A_Err SPAPI get_item_name(AEGP_PluginID plugin, AEGP_ItemH item, AEGP_MemHandle* output) {
    require(plugin == kPlugin && (item == current->primary || item == current->secondary), "wrong item name identity");
    const auto error = current->step("item-name");
    if ((!error || current->partial_handle_on_error) && !current->null_name) {
        auto* memory = new NameMemory;
        const auto& name = item == current->primary ? current->primary_name : current->secondary_name;
        memory->text.assign(name.begin(), name.end());
        if (!current->unterminated_name) memory->text.push_back(0);
        current->live_names.insert(memory); *output = reinterpret_cast<AEGP_MemHandle>(memory);
    }
    return error;
}
static NameMemory* as_name(AEGP_MemHandle value) { return reinterpret_cast<NameMemory*>(value); }
static A_Err SPAPI name_size(AEGP_MemHandle handle, AEGP_MemSize* output) {
    require(current->live_names.contains(as_name(handle)), "measured invalid name memory");
    if (const auto error = current->step("name-size")) return error;
    *output = static_cast<AEGP_MemSize>(as_name(handle)->text.size() * sizeof(A_UTF16Char) +
        (current->odd_name_size ? 1 : 0));
    return A_Err_NONE;
}
static A_Err SPAPI lock_name(AEGP_MemHandle handle, void** output) {
    require(current->live_names.contains(as_name(handle)) && !as_name(handle)->locked, "locked invalid name memory");
    if (const auto error = current->step("lock-name")) return error;
    as_name(handle)->locked = true;
    *output = current->null_name_pointer ? nullptr : as_name(handle)->text.data(); return A_Err_NONE;
}
static A_Err SPAPI unlock_name(AEGP_MemHandle handle) {
    require(current->live_names.contains(as_name(handle)) && as_name(handle)->locked, "unlocked invalid name memory");
    as_name(handle)->locked = false; current->cleanup.push_back("unlock-name"); return current->step("unlock-name");
}
static A_Err SPAPI free_name(AEGP_MemHandle handle) {
    require(current->live_names.contains(as_name(handle)) && !as_name(handle)->locked, "freed invalid or locked name memory");
    current->live_names.erase(as_name(handle)); delete as_name(handle);
    current->cleanup.push_back("free-name"); return current->step("free-name");
}
static A_Err SPAPI get_time(AEGP_ItemH value, A_Time* time) {
    require(value == current->primary || value == current->secondary, "wrong item time query");
    if (const auto error = current->step("time")) return error;
    *time = current->time; return A_Err_NONE;
}
static A_Err SPAPI get_layer_time(AEGP_LayerH layer, AEGP_LTimeMode mode, A_Time* time) {
    require(layer == current->active_layer && mode == AEGP_LTimeMode_CompTime,
        "layer time was not read in composition coordinates");
    if (const auto error = current->step("layer-time")) return error;
    *time = current->layer_time; return A_Err_NONE;
}
static A_Err SPAPI get_par(AEGP_ItemH value, A_Ratio* par) {
    require(value == current->primary || value == current->secondary, "wrong item PAR query");
    if (const auto error = current->step("par")) return error;
    *par = current->par; return A_Err_NONE;
}
static A_Err SPAPI new_options(AEGP_PluginID plugin, AEGP_ItemH item, AEGP_RenderOptionsH* output) {
    require(plugin == kPlugin && (item == current->primary || item == current->secondary), "wrong NewFromItem identity");
    const auto error = current->step("new-options");
    if ((!error || current->partial_handle_on_error) && !current->null_options) {
        auto* value = new Options; value->item = item;
        current->live_options.insert(value); *output = reinterpret_cast<AEGP_RenderOptionsH>(value);
    }
    return error;
}
static A_Err SPAPI dispose_options(AEGP_RenderOptionsH handle) {
    auto* value = as_options(handle);
    require(current->live_options.erase(value) == 1, "disposed invalid options");
    delete value; current->cleanup.push_back("options"); return current->step("dispose-options");
}
static A_Err SPAPI set_time(AEGP_RenderOptionsH handle, A_Time value) {
    if (const auto error = current->step("set-time")) return error;
    as_options(handle)->time = value; return A_Err_NONE;
}
static A_Err SPAPI set_world_type(AEGP_RenderOptionsH handle, AEGP_WorldType value) {
    if (const auto error = current->step("set-world")) return error;
    as_options(handle)->type = value; return A_Err_NONE;
}
static A_Err SPAPI set_matte(AEGP_RenderOptionsH handle, AEGP_MatteMode value) {
    if (const auto error = current->step("set-matte")) return error;
    as_options(handle)->matte = value; return A_Err_NONE;
}
static A_Err SPAPI set_order(AEGP_RenderOptionsH handle, AEGP_ChannelOrder value) {
    if (const auto error = current->step("set-order")) return error;
    as_options(handle)->order = value; return A_Err_NONE;
}
static A_Err SPAPI set_downsample(AEGP_RenderOptionsH handle, A_short x, A_short y) {
    ++current->option_downsample_calls;
    if (const auto error = current->step("set-downsample")) return error;
    as_options(handle)->down_x = x; as_options(handle)->down_y = y; return A_Err_NONE;
}
static A_Err SPAPI render_frame(AEGP_RenderOptionsH handle, AEGP_RenderSuiteCheckForCancel, AEGP_CancelRefcon,
                                AEGP_FrameReceiptH* output) {
    const auto error = current->step("render");
    if ((error && !current->partial_handle_on_error) || current->null_receipt) return error;
    const auto& options = *as_options(handle);
    auto* frame = new Receipt;
    frame->width = (options.item == current->primary ? current->render_width : 10) / options.down_x;
    const A_long bytes_per_pixel = options.type == AEGP_WorldType_8 ? 4 : options.type == AEGP_WorldType_16 ? 8 : 16;
    frame->height = current->render_height / options.down_y;
    frame->stride = static_cast<A_u_long>(frame->width * bytes_per_pixel + (options.type == AEGP_WorldType_8 ? 12 : 13));
    frame->type = current->wrong_world_type ? AEGP_WorldType_16 : options.type;
    frame->pixels.assign(static_cast<size_t>(frame->stride) * frame->height, 0xf3);
    for (A_long y = 0; y < frame->height; ++y) for (A_long x = 0; x < frame->width; ++x) {
        std::array<std::uint8_t, 4> pixel{255, static_cast<std::uint8_t>(40 + x * 7),
            static_cast<std::uint8_t>(60 + y * 9), 80};
        if (x == 0 && y == 0) pixel = {0, 17, 29, 31};
        if (x == 1 && y == 0) pixel = {128, 200, 50, 25};
        if (x == 0 && y == 1) pixel = {255, static_cast<std::uint8_t>(options.time.value), 16, 240};
        if (options.matte != AEGP_MatteMode_STRAIGHT)
            for (size_t channel = 1; channel < 4; ++channel) pixel[channel] = static_cast<std::uint8_t>(pixel[channel] * pixel[0] / 255);
        auto* address = frame->pixels.data() + static_cast<size_t>(y) * frame->stride + x * bytes_per_pixel;
        if (options.type == AEGP_WorldType_8) {
            if (options.order == AEGP_ChannelOrder_ARGB) std::copy(pixel.begin(), pixel.end(), address);
            else { address[0] = pixel[3]; address[1] = pixel[2]; address[2] = pixel[1]; address[3] = pixel[0]; }
        } else if (options.type == AEGP_WorldType_16) {
            const PF_Pixel16 value{static_cast<A_u_short>(x == 0 && y == 0 ? 0 : x == 1 && y == 0 ? 16384 : 32768),
                32768, 0, 0};
            std::memcpy(address, &value, sizeof(value));
        } else {
            const PF_PixelFloat value{x == 0 && y == 0 ? 0.0f : x == 1 && y == 0 ? 0.5f : 1.0f, 1.0f, 0.0f, 0.0f};
            std::memcpy(address, &value, sizeof(value));
        }
        const auto index = static_cast<size_t>(y) * frame->width + x;
        if (options.type == AEGP_WorldType_8 && !current->byte_fixture.empty()) {
            const auto& value = current->byte_fixture[index % current->byte_fixture.size()]; std::memcpy(address, value.data(), sizeof(value));
        }
        if (options.type == AEGP_WorldType_16 && !current->word_fixture.empty()) {
            const auto& value = current->word_fixture[index % current->word_fixture.size()]; std::memcpy(address, value.data(), sizeof(value));
        }
        if (options.type == AEGP_WorldType_32 && !current->float_fixture.empty()) {
            const auto& value = current->float_fixture[index % current->float_fixture.size()]; std::memcpy(address, value.data(), sizeof(value));
        }
    }
    frame->before = frame->pixels; current->live_receipts.insert(frame);
    *output = reinterpret_cast<AEGP_FrameReceiptH>(frame); return error;
}
static A_Err SPAPI checkin(AEGP_FrameReceiptH handle) {
    auto* value = reinterpret_cast<Receipt*>(handle);
    require(current->live_receipts.erase(value) == 1, "checked in invalid frame receipt");
    current->source_modified = current->source_modified || value->pixels != value->before;
    std::fill(value->pixels.begin(), value->pixels.end(), 0xee);
    delete value; current->cleanup.push_back("receipt"); return current->step("checkin");
}
static A_Err SPAPI get_world(AEGP_FrameReceiptH value, AEGP_WorldH* world) {
    if (const auto error = current->step("world")) return error;
    *world = current->null_world ? nullptr : reinterpret_cast<AEGP_WorldH>(value); return A_Err_NONE;
}
static A_Err SPAPI get_world_type(AEGP_WorldH value, AEGP_WorldType* type) {
    if (const auto error = current->step("world-type")) return error;
    *type = as_world(value)->type; return A_Err_NONE;
}
static A_Err SPAPI get_size(AEGP_WorldH value, A_long* width, A_long* height) {
    if (const auto error = current->step("world-size")) return error;
    *width = current->huge_size ? 2147483647 : current->invalid_size ? 0 : as_world(value)->width;
    *height = as_world(value)->height; return A_Err_NONE;
}
static A_Err SPAPI get_stride(AEGP_WorldH value, A_u_long* stride) {
    if (const auto error = current->step("world-stride")) return error;
    *stride = current->short_stride ? 4 : as_world(value)->stride; return A_Err_NONE;
}
static A_Err SPAPI get_base(AEGP_WorldH value, PF_Pixel8** base) {
    if (const auto error = current->step("world-base")) return error;
    *base = current->null_base ? nullptr : reinterpret_cast<PF_Pixel8*>(as_world(value)->pixels.data()); return A_Err_NONE;
}
static A_Err SPAPI get_base16(AEGP_WorldH value, PF_Pixel16** base) {
    if (const auto error = current->step("world-base16")) return error;
    *base = current->null_base ? nullptr : reinterpret_cast<PF_Pixel16*>(as_world(value)->pixels.data()); return A_Err_NONE;
}
static A_Err SPAPI get_base32(AEGP_WorldH value, PF_PixelFloat** base) {
    if (const auto error = current->step("world-base32")) return error;
    *base = current->null_base ? nullptr : reinterpret_cast<PF_PixelFloat*>(as_world(value)->pixels.data()); return A_Err_NONE;
}
static A_Err SPAPI get_view(AEGP_ItemH item, AEGP_ItemViewP* value) {
    require(item == current->primary || item == current->secondary, "wrong item view query");
    if (const auto error = current->step("view")) return error;
    *value = current->view; return A_Err_NONE;
}
static A_Err SPAPI has_transform(AEGP_ItemViewP view, A_Boolean* value) {
    require(view && view == current->view, "wrong or null item view");
    if (const auto error = current->step("has-transform")) return error;
    *value = current->view_has_transform ? TRUE : FALSE; return A_Err_NONE;
}
static A_Err SPAPI new_profile(AEGP_PluginID plugin, AEGP_CompH comp, AEGP_ColorProfileP* output) {
    require(plugin == kPlugin && comp == current->parent_comp, "wrong working-profile identity");
    const auto error = current->step("new-profile");
    if ((!error || current->partial_handle_on_error) && !current->null_profile) {
        *output = reinterpret_cast<AEGP_ColorProfileP>(new int(17)); current->live_profiles.insert(*output);
    }
    return error;
}
static A_Err SPAPI dispose_profile(AEGP_ColorProfileP profile) {
    require(current->live_profiles.erase(profile) == 1, "disposed an invalid working profile");
    delete reinterpret_cast<int*>(profile); current->cleanup.push_back("profile");
    return current->step("dispose-profile");
}
static A_Err SPAPI new_world(AEGP_PluginID plugin, AEGP_WorldType type, A_long width, A_long height,
                            AEGP_WorldH* output) {
    require(plugin == kPlugin && width > 0 && height > 0, "wrong owned-world request");
    const auto error = current->step("new-world");
    if ((!error || current->partial_handle_on_error) && !current->null_owned_world) {
        auto* value = new Receipt; value->type = type; value->width = width; value->height = height;
        value->stride = static_cast<A_u_long>(width * (type == AEGP_WorldType_8 ? 4 : type == AEGP_WorldType_16 ? 8 : 16) + 19);
        value->pixels.assign(static_cast<size_t>(value->stride) * height, 0xda);
        current->live_worlds.insert(value); *output = reinterpret_cast<AEGP_WorldH>(value);
    }
    return error;
}
static A_Err SPAPI dispose_world(AEGP_WorldH world) {
    auto* value = as_world(world);
    if (current->live_receipts.contains(value)) { ++current->borrowed_world_disposals; return kInjected; }
    require(current->live_worlds.erase(value) == 1, "disposed an invalid owned world");
    delete value; current->cleanup.push_back("owned-world"); return current->step("dispose-world");
}
static A_Err SPAPI transform(AEGP_ItemViewP view, AEGP_WorldH source, AEGP_WorldH destination) {
    require(view == current->view && source != destination && current->live_receipts.contains(as_world(source)) &&
        current->live_worlds.contains(as_world(destination)), "transform did not use a separate owned destination world");
    if (const auto error = current->step("transform")) return error;
    const auto& input = *as_world(source); auto& output = *as_world(destination);
    require(input.width == output.width && input.height == output.height && input.type == output.type,
        "transform destination shape or depth was wrong");
    const size_t bytes = input.type == AEGP_WorldType_8 ? 4 : input.type == AEGP_WorldType_16 ? 8 : 16;
    for (A_long y = 0; y < output.height; ++y) for (A_long x = 0; x < output.width; ++x) {
        auto* address = output.pixels.data() + static_cast<size_t>(y) * output.stride + static_cast<size_t>(x) * bytes;
        if (input.type == AEGP_WorldType_8) {
            const PF_Pixel8 value{0, 0, 0, 255}; std::memcpy(address, &value, sizeof(value));
        } else if (input.type == AEGP_WorldType_16) {
            const PF_Pixel16 value{0, 0, 0, 32768}; std::memcpy(address, &value, sizeof(value));
        } else {
            const PF_PixelFloat value{0.0f, 0.0f, 0.0f, 1.0f}; std::memcpy(address, &value, sizeof(value));
        }
    }
    return A_Err_NONE;
}
static A_Err SPAPI forbidden_comp_downsample(AEGP_CompH, const AEGP_DownsampleFactor*) {
    ++current->project_mutations; return kInjected;
}
FakeHost::FakeHost() {
    require(!current, "nested fake host"); current = this;
    basic.AcquireSuite = acquire; basic.ReleaseSuite = release;
    item.AEGP_GetActiveItem = get_active_item; item.AEGP_GetItemType = get_item_type;
    item.AEGP_GetItemName = get_item_name;
    memory.AEGP_FreeMemHandle = free_name; memory.AEGP_LockMemHandle = lock_name;
    memory.AEGP_UnlockMemHandle = unlock_name; memory.AEGP_GetMemHandleSize = name_size;
    item.AEGP_GetItemCurrentTime = get_time; item.AEGP_GetItemPixelAspectRatio = get_par;
    layer.AEGP_GetActiveLayer = get_active_layer; layer.AEGP_GetLayerObjectType = get_layer_type;
    layer.AEGP_GetLayerParentComp = get_parent_comp; comp.AEGP_GetItemFromComp = get_item_from_comp;
    comp.AEGP_GetCompFromItem = get_comp_from_item; layer.AEGP_GetLayerCurrentTime = get_layer_time;
    project_suite.AEGP_GetProjectByIndex = get_project; project_suite.AEGP_GetProjectBitDepth = get_depth;
    item.AEGP_GetItemMRUView = get_view; color.AEGP_DoesViewHaveColorSpaceXform = has_transform;
    color.AEGP_GetNewWorkingSpaceColorProfile = new_profile; color.AEGP_DisposeColorProfile = dispose_profile;
    color.AEGP_XformWorkingToViewColorSpace = transform;
    comp.AEGP_SetCompDownsampleFactor = forbidden_comp_downsample;
    options.AEGP_NewFromItem = new_options; options.AEGP_Dispose = dispose_options;
    options.AEGP_SetTime = set_time; options.AEGP_SetWorldType = set_world_type;
    options.AEGP_SetMatteMode = set_matte; options.AEGP_SetChannelOrder = set_order;
    options.AEGP_SetDownsampleFactor = set_downsample;
    render.AEGP_RenderAndCheckoutFrame = render_frame; render.AEGP_CheckinFrame = checkin; render.AEGP_GetReceiptWorld = get_world;
    world.AEGP_GetType = get_world_type; world.AEGP_GetSize = get_size;
    world.AEGP_GetRowBytes = get_stride; world.AEGP_GetBaseAddr8 = get_base;
    world.AEGP_GetBaseAddr16 = get_base16; world.AEGP_GetBaseAddr32 = get_base32;
    world.AEGP_New = new_world; world.AEGP_Dispose = dispose_world;
}

struct Decoded { UINT width = 0, height = 0; std::vector<std::uint8_t> pixels; };
static Decoded decode(const fs::path& file) {
    std::ifstream input(file, std::ios::binary); std::array<unsigned char, 8> signature{};
    input.read(reinterpret_cast<char*>(signature.data()), signature.size());
    require(signature == std::array<unsigned char, 8>{137, 80, 78, 71, 13, 10, 26, 10}, "capture is not a PNG file");
    ComPtr<IWICImagingFactory> factory;
    check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "decoder factory failed");
    ComPtr<IWICBitmapDecoder> decoder;
    check_hr(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder), "PNG decode failed");
    ComPtr<IWICBitmapFrameDecode> frame; check_hr(decoder->GetFrame(0, &frame), "PNG frame missing");
    Decoded value; check_hr(frame->GetSize(&value.width, &value.height), "PNG dimensions missing");
    ComPtr<IWICFormatConverter> converted; check_hr(factory->CreateFormatConverter(&converted), "PNG converter failed");
    check_hr(converted->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
        WICBitmapPaletteTypeCustom), "PNG BGRA conversion failed");
    value.pixels.resize(static_cast<size_t>(value.width) * value.height * 4);
    check_hr(converted->CopyPixels(nullptr, value.width * 4, static_cast<UINT>(value.pixels.size()), value.pixels.data()), "PNG pixels missing");
    return value;
}
static std::array<std::uint8_t, 4> pixel(const Decoded& value, UINT x, UINT y) {
    const auto offset = (static_cast<size_t>(y) * value.width + x) * 4;
    return {value.pixels[offset], value.pixels[offset + 1], value.pixels[offset + 2], value.pixels[offset + 3]};
}
static void clean_temp_files(const fs::path& directory) {
    for (const auto& entry : fs::directory_iterator(directory))
        require(entry.path().extension() == L".png", "failed capture left a temporary file");
}
static std::string read_file(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
static void old_target(const fs::path& file) { std::ofstream(file, std::ios::binary) << "previous complete capture"; }
static void failed_capture(const xfx::ScriptReply& reply, const fs::path& target, const fs::path& directory) {
    require(!reply.succeeded && !reply.partial && reply.count == 0 && !reply.message.empty(),
        "capture failure did not provide an explicit failed reply");
    require(read_file(target) == "previous complete capture", "failed capture altered the existing target");
    clean_temp_files(directory);
}
struct MxcsrScope {
    unsigned int saved = _mm_getcsr();
    explicit MxcsrScope(unsigned int rounding) { _mm_setcsr((saved & ~_MM_ROUND_MASK) | rounding); }
    ~MxcsrScope() { _mm_setcsr(saved); }
};

int wmain() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    require(SUCCEEDED(initialized), "test COM initialization failed");
    const auto directory = fs::temp_directory_path() / (L"xfx-native-capture-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    require(fs::create_directory(directory), "test directory already exists");
    int failures = 0;
    const auto run = [&](const char* name, const std::function<void()>& test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    run("capture name returns full Unicode without acquiring render interfaces", [&] {
        FakeHost host;
        host.render.AEGP_RenderAndCheckoutFrame = nullptr; host.project_suite.AEGP_GetProjectBitDepth = nullptr;
        const auto value = xfx::current_capture_comp_name(&host.basic, kPlugin);
        require(value.reply.succeeded && value.name == L"当前合成_中文_\U0001F3AC", "Unicode composition name was not returned intact");
        require(host.calls["render"] == 0 && host.calls["new-options"] == 0 && host.calls["project"] == 0,
            "reading a save filename rendered pixels or inspected project depth");
        require(host.calls["acquire:" + std::string(kAEGPRenderSuite) + ':' + std::to_string(kAEGPRenderSuiteVersion3)] == 0,
            "reading a save filename acquired render interfaces");
        host.released();
    });
    run("capture name prefers renderable active layer parent over project selection", [&] {
        for (const auto type : {AEGP_ObjectType_AV, AEGP_ObjectType_TEXT, AEGP_ObjectType_VECTOR}) {
            FakeHost host; host.active_layer = fake_handle<AEGP_LayerH>(0x4000); host.layer_type = type;
            host.active_item = host.secondary;
            const auto value = xfx::current_capture_comp_name(&host.basic, kPlugin);
            require(value.reply.succeeded && value.name == L"当前合成_中文_\U0001F3AC", "filename used unrelated selected project item");
            host.released();
        }
    });
    run("capture name falls back to active composition for camera and light layers", [&] {
        for (const auto type : {AEGP_ObjectType_CAMERA, AEGP_ObjectType_LIGHT}) {
            FakeHost host; host.active_layer = fake_handle<AEGP_LayerH>(0x4000); host.layer_type = type;
            host.active_item = host.secondary;
            const auto value = xfx::current_capture_comp_name(&host.basic, kPlugin);
            require(value.reply.succeeded && value.name == L"其他合成", "non-renderable layer prevented active composition fallback");
            host.released();
        }
    });
    const auto failed_name = [](const xfx::CaptureCompName& value) {
        require(!value.reply.succeeded && !value.reply.partial && value.name.empty() && !value.reply.message.empty(),
            "name failure returned a usable filename or omitted its error");
    };
    run("capture name rejects missing composition or footage before reading its name", [&] {
        for (int scenario = 0; scenario < 2; ++scenario) {
            FakeHost host;
            if (scenario == 0) host.active_item = nullptr;
            if (scenario == 1) host.item_type = AEGP_ItemType_FOOTAGE;
            failed_name(xfx::current_capture_comp_name(&host.basic, kPlugin));
            require(host.calls["active-item"] == 1 && host.calls["item-name"] == 0, "invalid selection reached name allocation");
            host.released();
        }
    });
    run("capture name operation failures release partial name allocations", [&] {
        for (const auto* step : {"item-name", "name-size", "lock-name"}) {
            FakeHost host; host.fail = step; host.partial_handle_on_error = true;
            failed_name(xfx::current_capture_comp_name(&host.basic, kPlugin));
            require(host.calls[step] == 1, "name error boundary was not reached"); host.released();
        }
    });
    run("capture name cleanup errors fail and still release every acquired suite", [&] {
        std::vector<std::string> steps{"unlock-name", "free-name"};
        for (const auto& suite : {std::pair{kAEGPItemSuite, kAEGPItemSuiteVersion8}, std::pair{kAEGPLayerSuite, kAEGPLayerSuiteVersion8},
                std::pair{kAEGPCompSuite, kAEGPCompSuiteVersion7}, std::pair{kAEGPMemorySuite, kAEGPMemorySuiteVersion1}})
            steps.push_back("release:" + std::string(suite.first) + ':' + std::to_string(suite.second));
        for (const auto& step : steps) {
            FakeHost host; host.fail = step;
            failed_name(xfx::current_capture_comp_name(&host.basic, kPlugin));
            require(host.calls[step] == 1, "name cleanup failure boundary was not reached"); host.released();
        }
    });
    run("capture name rejects null handles null pointers and malformed UTF16 buffers", [&] {
        for (int scenario = 0; scenario < 4; ++scenario) {
            FakeHost host;
            if (scenario == 0) host.null_name = true;
            if (scenario == 1) host.null_name_pointer = true;
            if (scenario == 2) host.unterminated_name = true;
            if (scenario == 3) host.odd_name_size = true;
            failed_name(xfx::current_capture_comp_name(&host.basic, kPlugin));
            require(host.calls["item-name"] == 1, "invalid name buffer case did not read a name"); host.released();
        }
    });
    run("capture name validates host and only required metadata function pointers", [&] {
        failed_name(xfx::current_capture_comp_name(nullptr, kPlugin));
        for (int scenario = 0; scenario < 7; ++scenario) {
            FakeHost host;
            if (scenario == 0) host.basic.AcquireSuite = nullptr;
            if (scenario == 1) host.basic.ReleaseSuite = nullptr;
            if (scenario == 2) host.item.AEGP_GetItemName = nullptr;
            if (scenario == 3) host.memory.AEGP_LockMemHandle = nullptr;
            if (scenario == 4) host.memory.AEGP_UnlockMemHandle = nullptr;
            if (scenario == 5) host.memory.AEGP_FreeMemHandle = nullptr;
            if (scenario == 6) host.memory.AEGP_GetMemHandleSize = nullptr;
            failed_name(xfx::current_capture_comp_name(&host.basic, kPlugin)); host.released();
        }
    });
    run("project depth chooses 8 16 and float padded render worlds", [&] {
        for (const auto depth : {AEGP_ProjBitDepth_8, AEGP_ProjBitDepth_16, AEGP_ProjBitDepth_32}) {
            FakeHost host; host.project_depth = static_cast<AEGP_ProjBitDepth>(depth);
            const auto target = directory / (L"project-depth-" + std::to_wstring(depth) + L".png");
            require(xfx::capture_comp_png(&host.basic, kPlugin, target, false).succeeded, "project-depth capture failed");
            const auto image = decode(target);
            require(image.width == 3 && image.height == 2, "project depth changed default downsample dimensions");
            if (depth != AEGP_ProjBitDepth_8)
                require(pixel(image, 2, 1) == std::array<std::uint8_t, 4>{0, 0, 255, 255},
                    "project depth was forced to 8-bit or high-depth padded pixels were misread");
            host.released();
        }
    });
    run("active layer composition time changes captured pixels independently of item time", [&] {
        for (const auto type : {AEGP_ObjectType_AV, AEGP_ObjectType_TEXT, AEGP_ObjectType_VECTOR}) {
            FakeHost host; host.active_layer = fake_handle<AEGP_LayerH>(0x4000); host.layer_type = type;
            const auto target = directory / (L"layer-time-" + std::to_wstring(type) + L".png");
            require(xfx::capture_comp_png(&host.basic, kPlugin, target, false).succeeded, "layer time capture failed");
            require(pixel(decode(target), 0, 1)[2] == 77, "capture ignored independent layer composition time");
            host.released();
        }
    });
    run("view transform changes decoded RGB and preserves borrowed alpha at all depths", [&] {
        for (const auto depth : {AEGP_ProjBitDepth_8, AEGP_ProjBitDepth_16, AEGP_ProjBitDepth_32}) {
            FakeHost host; host.project_depth = static_cast<AEGP_ProjBitDepth>(depth); host.view_has_transform = true;
            const auto target = directory / (L"view-color-" + std::to_wstring(depth) + L".png");
            require(xfx::capture_comp_png(&host.basic, kPlugin, target, false).succeeded, "view transform capture failed");
            const auto image = decode(target);
            require(pixel(image, 0, 1) == std::array<std::uint8_t, 4>{255, 0, 0, 255},
                "view RGB was not transformed or transform damaged the original alpha");
            require(pixel(image, 0, 0)[3] == 0, "original zero alpha was not preserved across view conversion");
            if (depth == AEGP_ProjBitDepth_8)
                require(pixel(image, 1, 0)[3] == 128, "original half alpha was lost during color conversion");
            const auto world = std::find(host.cleanup.begin(), host.cleanup.end(), "owned-world");
            const auto profile = std::find(host.cleanup.begin(), host.cleanup.end(), "profile");
            const auto receipt = std::find(host.cleanup.begin(), host.cleanup.end(), "receipt");
            const auto options = std::find(host.cleanup.begin(), host.cleanup.end(), "options");
            require(world != host.cleanup.end() && profile != host.cleanup.end() &&
                receipt != host.cleanup.end() && options != host.cleanup.end() &&
                world < receipt && profile < receipt && receipt < options &&
                world < options && profile < options,
                "owned color resources were not released before receipt/options");
            host.released();
        }
    });
    run("disabled view transform leaves decoded working pixels intact", [&] {
        FakeHost host; const auto target = directory / L"no-view-transform.png";
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, false).succeeded, "non-transform capture failed");
        require(pixel(decode(target), 1, 0) == std::array<std::uint8_t, 4>{25, 50, 200, 128},
            "disabled view transform still changed working RGB");
        require(host.calls["new-world"] == 0 && host.calls["new-profile"] == 0,
            "disabled transform allocated unnecessary owned color resources");
        host.released();
    });
    run("missing project invalid depth and unavailable view preserve existing capture", [&] {
        for (int scenario = 0; scenario < 3; ++scenario) {
            FakeHost host; const auto target = directory / L"failure.png"; old_target(target);
            if (scenario == 0) host.project = nullptr;
            if (scenario == 1) host.project_depth = static_cast<AEGP_ProjBitDepth>(3);
            if (scenario == 2) host.view = nullptr;
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, false), target, directory);
            host.released();
        }
    });
    run("new project time and color API errors never publish incomplete captures", [&] {
        for (const auto* step : {"project", "depth", "item-comp", "layer-time", "view", "has-transform",
                "new-profile", "new-world", "transform", "world-base16", "world-base32"}) {
            FakeHost host; host.view_has_transform = true; host.fail = step;
            if (host.fail == "layer-time") host.active_layer = fake_handle<AEGP_LayerH>(0x4000);
            if (host.fail == "world-base16") host.project_depth = AEGP_ProjBitDepth_16;
            if (host.fail == "world-base32") host.project_depth = AEGP_ProjBitDepth_32;
            const auto target = directory / L"failure.png"; old_target(target);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            require(host.calls[step] == 1, "new SDK failure boundary was not reached"); host.released();
        }
    });
    run("partial profile and world handles unwind without replacing old PNG", [&] {
        for (const auto* step : {"new-profile", "new-world"}) {
            FakeHost host; host.view_has_transform = true; host.fail = step; host.partial_handle_on_error = true;
            const auto target = directory / L"failure.png"; old_target(target);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            require(host.calls[step] == 1, "partial color resource boundary was not reached"); host.released();
        }
    });
    run("owned color cleanup failures prevent publication and continue later cleanup", [&] {
        for (const auto* step : {"dispose-world", "dispose-profile"}) {
            FakeHost host; host.view_has_transform = true; host.fail = step;
            const auto target = directory / L"failure.png"; old_target(target);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            require(host.calls[step] == 1, "owned color cleanup failure boundary was not reached"); host.released();
        }
    });
    run("all 256 eight-bit samples round trip through original float normalization", [&] {
        MxcsrScope rounding(_MM_ROUND_NEAREST);
        FakeHost host; host.render_width = 256; host.render_height = 1;
        for (unsigned int value = 0; value < 256; ++value) {
            const auto channel = static_cast<A_u_char>(value); host.byte_fixture.push_back({channel, channel, channel, channel});
        }
        const auto target = directory / L"all-eight-bit.png";
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "eight-bit exhaustive PNG capture failed");
        const auto image = decode(target);
        require(image.width == 256 && image.height == 1, "exhaustive fixture dimensions changed");
        for (unsigned int value = 0; value < 256; ++value) {
            const auto channel = static_cast<std::uint8_t>(value);
            require(pixel(image, value, 0) == std::array<std::uint8_t, 4>{channel, channel, channel, channel},
                "an eight-bit value failed float normalization round trip");
        }
        host.released();
    });
    run("sixteen-bit original scale and saturation produce decoded PNG literals", [&] {
        MxcsrScope rounding(_MM_ROUND_NEAREST);
        FakeHost host; host.project_depth = AEGP_ProjBitDepth_16; host.render_width = 4; host.render_height = 1;
        host.word_fixture = {{32767, 0, 16384, 32767}, {32768, 32767, 32768, 65535},
            {16384, 128, 64, 1}, {65535, 32703, 0, 0}};
        const auto target = directory / L"word-quantization.png";
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "sixteen-bit PNG capture failed");
        const auto image = decode(target);
        const std::array<std::array<std::uint8_t, 4>, 4> expected{{{255, 128, 0, 255}, {255, 255, 255, 255},
            {0, 0, 1, 128}, {0, 0, 255, 255}}};
        for (UINT x = 0; x < expected.size(); ++x)
            require(pixel(image, x, 0) == expected[x], "sixteen-bit original scale or packed saturation is wrong");
        host.released();
    });
    run("float nearest-even HDR negative and nonfinite samples follow packed SSE output", [&] {
        MxcsrScope rounding(_MM_ROUND_NEAREST);
        FakeHost host; host.project_depth = AEGP_ProjBitDepth_32; host.render_width = 4; host.render_height = 1;
        host.float_fixture = {{1.0f, 0.5f, 0.1f, 0.3f}, {0.5f, -0.1f, 2.0f, 0.0f},
            {1.0f, (std::numeric_limits<float>::quiet_NaN)(), (std::numeric_limits<float>::infinity)(),
                -(std::numeric_limits<float>::infinity)()}, {1.0f, 1.0e20f, 1.0f, 1.0f}};
        const auto target = directory / L"float-quantization.png";
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "float PNG capture failed");
        const auto image = decode(target);
        const std::array<std::array<std::uint8_t, 4>, 4> expected{{{76, 26, 128, 255}, {0, 255, 0, 128},
            {0, 0, 0, 255}, {255, 255, 0, 255}}};
        for (UINT x = 0; x < expected.size(); ++x)
            require(pixel(image, x, 0) == expected[x], "float nearest-even or nonfinite packed result is wrong");
        require((_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_NEAREST, "capture changed MXCSR rounding mode");
        host.released();
    });
    run("float quantization honors caller MXCSR downward rounding without changing it", [&] {
        MxcsrScope rounding(_MM_ROUND_DOWN);
        FakeHost host; host.project_depth = AEGP_ProjBitDepth_32; host.render_width = 1; host.render_height = 1;
        host.float_fixture = {{1.0f, 0.5f, 0.1f, 0.3f}};
        const auto target = directory / L"float-downward.png";
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "downward-rounded float PNG capture failed");
        require(pixel(decode(target), 0, 0) == std::array<std::uint8_t, 4>{76, 25, 127, 255},
            "float quantization used truncation or replaced caller rounding mode");
        require((_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_DOWN, "capture changed caller MXCSR rounding mode");
        host.released();
    });
    run("default downsample, current time, straight alpha and row padding", [&] {
        FakeHost host; const auto target = directory / L"中文默认截图.png";
        const auto reply = xfx::capture_comp_png(&host.basic, kPlugin, target, false);
        require(reply.succeeded && !reply.partial && reply.count == 1, "capture did not return a complete PNG success");
        const auto image = decode(target);
        require(image.width == 3 && image.height == 2, "default options downsample was overwritten");
        require(pixel(image, 0, 0)[3] == 0, "transparent pixel became opaque");
        require(pixel(image, 1, 0) == std::array<std::uint8_t, 4>{25, 50, 200, 128}, "straight alpha/channel color was changed");
        require(pixel(image, 0, 1) == std::array<std::uint8_t, 4>{240, 16, 125, 255}, "current time or padded row stride was ignored");
        require(host.option_downsample_calls == 0, "default capture called SetDownsampleFactor");
        host.released();
        require(host.cleanup.size() >= 2 && host.cleanup[0] == "receipt" && host.cleanup[1] == "options", "frame/options cleanup order was wrong");
    });
    run("full resolution affects options and final PNG size", [&] {
        FakeHost host; const auto target = directory / L"full.png";
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "full resolution capture failed");
        const auto image = decode(target);
        require(image.width == 6 && image.height == 4 && host.option_downsample_calls == 1,
            "full resolution did not request a 1x1 options downsample");
        host.released();
    });
    run("active renderable layer selects its parent comp before active item", [&] {
        for (const auto type : {AEGP_ObjectType_AV, AEGP_ObjectType_TEXT, AEGP_ObjectType_VECTOR}) {
            FakeHost host; host.active_layer = fake_handle<AEGP_LayerH>(0x4000); host.layer_type = type;
            host.active_item = host.secondary;
            const auto target = directory / (L"layer-" + std::to_wstring(type) + L".png");
            require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "active layer capture failed");
            require(decode(target).width == 6, "capture used unrelated active project item instead of layer parent comp");
            host.released();
        }
    });
    run("PAR greater and less than one truncates corrected width", [&] {
        struct Case { A_long numerator; A_u_long denominator; UINT expected; };
        for (const auto test : {Case{3, 2, 4}, Case{1, 2, 1}, Case{1, 1, 3}}) {
            FakeHost host; host.par = {test.numerator, test.denominator};
            const auto target = directory / (L"par-" + std::to_wstring(test.numerator) + L"-" + std::to_wstring(test.denominator) + L".png");
            require(xfx::capture_comp_png(&host.basic, kPlugin, target, false).succeeded, "PAR capture failed");
            const auto image = decode(target);
            require(image.width == test.expected && image.height == 2, "PAR correction rounded incorrectly or changed height");
            if (test.expected == 1) require(pixel(image, 0, 1) == std::array<std::uint8_t, 4>{133, 51, 75, 255},
                "Fant downsampling did not average the three opaque source pixels");
            host.released();
        }
    });
    run("suite acquisition errors unwind previously acquired real ABI tables", [&] {
        struct Suite { const char* name; int version; };
        const Suite suites[] = {{kAEGPItemSuite, kAEGPItemSuiteVersion8}, {kAEGPLayerSuite, kAEGPLayerSuiteVersion8},
            {kAEGPCompSuite, kAEGPCompSuiteVersion7}, {kAEGPRenderOptionsSuite, kAEGPRenderOptionsSuiteVersion3},
            {kAEGPRenderSuite, kAEGPRenderSuiteVersion3}, {kAEGPWorldSuite, kAEGPWorldSuiteVersion3},
            {kAEGPProjSuite, kAEGPProjSuiteVersion5}, {kAEGPColorSettingsSuite, kAEGPColorSettingsSuiteVersion2}};
        for (const auto& suite : suites) {
            FakeHost host; const auto target = directory / L"failure.png"; old_target(target);
            host.fail = "acquire:" + std::string(suite.name) + ':' + std::to_string(suite.version);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, false), target, directory);
            require(host.calls[host.fail] == 1, "capture did not reach the injected suite acquisition failure");
            host.released();
        }
    });
    run("SDK operation errors never publish and release owned resources", [&] {
        const char* steps[] = {"active-layer", "layer-type", "parent-comp", "comp-item", "active-item", "item-type",
            "time", "par", "new-options", "set-time", "set-world", "set-matte", "set-order", "set-downsample",
            "render", "world", "world-type", "world-size", "world-stride", "world-base"};
        for (const auto* step : steps) {
            FakeHost host; const auto target = directory / L"failure.png"; old_target(target); host.fail = step;
            if (host.fail == "layer-type" || host.fail == "parent-comp" || host.fail == "comp-item")
                host.active_layer = fake_handle<AEGP_LayerH>(0x4000);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            require(host.calls[step] == 1, "capture did not reach the injected SDK operation failure");
            host.released();
        }
    });
    run("partial error handles are still checked in and disposed", [&] {
        for (const auto* step : {"new-options", "render"}) {
            FakeHost host; host.fail = step; host.partial_handle_on_error = true;
            const auto target = directory / L"failure.png"; old_target(target);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            require(host.calls[step] == 1, "partial handle boundary was not exercised"); host.released();
        }
    });
    run("cleanup failures prevent success and do not stop other cleanup", [&] {
        std::vector<std::string> steps{"checkin", "dispose-options"};
        for (const auto& suite : {std::pair{kAEGPItemSuite, kAEGPItemSuiteVersion8}, std::pair{kAEGPLayerSuite, kAEGPLayerSuiteVersion8},
                std::pair{kAEGPCompSuite, kAEGPCompSuiteVersion7}, std::pair{kAEGPRenderOptionsSuite, kAEGPRenderOptionsSuiteVersion3},
                std::pair{kAEGPRenderSuite, kAEGPRenderSuiteVersion3}, std::pair{kAEGPWorldSuite, kAEGPWorldSuiteVersion3},
                std::pair{kAEGPProjSuite, kAEGPProjSuiteVersion5},
                std::pair{kAEGPColorSettingsSuite, kAEGPColorSettingsSuiteVersion2}})
            steps.push_back("release:" + std::string(suite.first) + ':' + std::to_string(suite.second));
        for (const auto& step : steps) {
            FakeHost host; host.fail = step; const auto target = directory / L"failure.png"; old_target(target);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            require(host.calls[step] == 1, "cleanup error boundary was not reached"); host.released();
        }
    });
    run("invalid PAR, world shape, pointers and time fail safely", [&] {
        for (int scenario = 0; scenario < 13; ++scenario) {
            FakeHost host; const auto target = directory / L"failure.png"; old_target(target);
            switch (scenario) {
            case 0: host.par = {0, 1}; break;
            case 1: host.par = {-1, 1}; break;
            case 2: host.par = {1, 0}; break;
            case 3: host.par = {1, 100}; break;
            case 4: host.par = {2147483647, 1}; break;
            case 5: host.null_options = true; break;
            case 6: host.null_receipt = true; break;
            case 7: host.null_world = true; break;
            case 8: host.null_base = true; break;
            case 9: host.wrong_world_type = true; break;
            case 10: host.invalid_size = true; break;
            case 11: host.short_stride = true; break;
            case 12: host.huge_size = true; break;
            }
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory); host.released();
        }
        FakeHost host; host.time.scale = 0; const auto target = directory / L"failure.png"; old_target(target);
        failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory); host.released();
    });
    run("no active composition and unavailable SDK functions return failure", [&] {
        for (int scenario = 0; scenario < 6; ++scenario) {
            FakeHost host; const auto target = directory / L"failure.png"; old_target(target);
            if (scenario == 0) host.active_item = nullptr;
            if (scenario == 1) host.item_type = AEGP_ItemType_FOOTAGE;
            if (scenario == 2) host.render.AEGP_GetReceiptWorld = nullptr;
            if (scenario == 3) host.basic.ReleaseSuite = nullptr;
            if (scenario == 4) host.project_suite.AEGP_GetProjectBitDepth = nullptr;
            if (scenario == 5) host.color.AEGP_XformWorkingToViewColorSpace = nullptr;
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory); host.released();
        }
        const auto target = directory / L"failure.png"; old_target(target);
        failed_capture(xfx::capture_comp_png(nullptr, kPlugin, target, true), target, directory);
    });
    run("missing project and color suite function pointers fail before host calls", [&] {
        struct MissingFunction {
            const char* name;
            void (*disable)(FakeHost&);
        };
        const MissingFunction cases[] = {
            {"project lookup", [](FakeHost& host) { host.project_suite.AEGP_GetProjectByIndex = nullptr; }},
            {"project depth", [](FakeHost& host) { host.project_suite.AEGP_GetProjectBitDepth = nullptr; }},
            {"view transform probe", [](FakeHost& host) { host.color.AEGP_DoesViewHaveColorSpaceXform = nullptr; }},
            {"working profile", [](FakeHost& host) { host.color.AEGP_GetNewWorkingSpaceColorProfile = nullptr; }},
            {"profile disposal", [](FakeHost& host) { host.color.AEGP_DisposeColorProfile = nullptr; }},
            {"working to view transform", [](FakeHost& host) { host.color.AEGP_XformWorkingToViewColorSpace = nullptr; }},
        };
        for (const auto& test : cases) {
            FakeHost host;
            test.disable(host);
            const auto target = directory / L"failure.png";
            old_target(target);
            failed_capture(xfx::capture_comp_png(&host.basic, kPlugin, target, true), target, directory);
            for (const auto& [call, count] : host.calls) {
                (void)count;
                if (call.rfind("acquire:", 0) != 0 && call.rfind("release:", 0) != 0)
                    throw std::runtime_error(std::string(test.name) + ": unexpected " + call);
            }
            host.released();
        }
    });
    run("non-renderable active layer falls back to active composition", [&] {
        for (const auto type : {AEGP_ObjectType_LIGHT, AEGP_ObjectType_CAMERA}) {
            FakeHost host; host.active_layer = fake_handle<AEGP_LayerH>(0x4000); host.layer_type = type;
            const auto target = directory / L"fallback.png";
            require(xfx::capture_comp_png(&host.basic, kPlugin, target, false).succeeded, "light/camera did not fall back to active comp");
            require(decode(target).width == 3, "fallback composition dimensions were wrong"); host.released();
        }
    });
    run("write failures preserve old file and remove temporary siblings", [&] {
        const auto target = directory / L"locked.png"; old_target(target);
        HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(locked != INVALID_HANDLE_VALUE, "cannot lock owned test target");
        {
            FakeHost host; const auto reply = xfx::capture_comp_png(&host.basic, kPlugin, target, true);
            CloseHandle(locked); locked = INVALID_HANDLE_VALUE;
            failed_capture(reply, target, directory); host.released();
        }
        FakeHost host;
        const auto missing = directory / L"missing-parent" / L"image.png";
        const auto reply = xfx::capture_comp_png(&host.basic, kPlugin, missing, true);
        require(!reply.succeeded && !reply.message.empty() && !fs::exists(missing.parent_path()), "capture silently created a missing target directory");
        clean_temp_files(directory); host.released();
    });
    run("read-only target and invalid destination never replace existing data", [&] {
        const auto target = directory / L"readonly.png"; old_target(target);
        require(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "cannot protect owned test target");
        {
            FakeHost host; const auto reply = xfx::capture_comp_png(&host.basic, kPlugin, target, true);
            SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
            failed_capture(reply, target, directory); host.released();
        }
        for (const auto& invalid : {fs::path{}, directory, fs::path(std::wstring(L"bad\0target.png", 14))}) {
            FakeHost host;
            const auto reply = xfx::capture_comp_png(&host.basic, kPlugin, invalid, true);
            require(!reply.succeeded && !reply.message.empty() && host.calls.empty(),
                "invalid capture destination reached AE or lacked a failure reply");
            host.released();
        }
    });
    run("successful atomic replacement leaves a valid complete PNG", [&] {
        FakeHost host; const auto target = directory / L"replace.png"; old_target(target);
        require(xfx::capture_comp_png(&host.basic, kPlugin, target, true).succeeded, "replacement capture failed");
        require(decode(target).width == 6, "successful replacement was not a complete PNG");
        clean_temp_files(directory); host.released();
    });
    run("long legal destination leaf saves a complete PNG beyond MAX_PATH", [&] {
        FakeHost host;
        const auto target = directory / (std::wstring(230, L'长') + L".png");
        const auto reply = xfx::capture_comp_png(&host.basic, kPlugin, target, true);
        require(reply.succeeded, "legal long filename failed during atomic PNG publication");
        const auto extended_target = fs::path(L"\\\\?\\" + fs::absolute(target).wstring());
        require(decode(extended_target).width == 6, "long filename did not contain a complete PNG");
        require(fs::remove(extended_target), "cannot remove owned long-path PNG fixture");
        clean_temp_files(directory); host.released();
    });
    clean_temp_files(directory);
    for (const auto& entry : fs::directory_iterator(directory)) fs::remove(entry.path());
    fs::remove(directory); CoUninitialize();
    return failures ? 1 : 0;
}
