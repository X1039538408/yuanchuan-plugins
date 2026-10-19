#include "ae_capture.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace xfx {
namespace {
using Microsoft::WRL::ComPtr;

[[noreturn]] void fail(const char* operation, long code = 0) {
    throw std::runtime_error(std::string(operation) + (code ? " (" + std::to_string(code) + ")" : ""));
}
void check_ae(A_Err error, const char* operation) { if (error != A_Err_NONE) fail(operation, error); }
void check_hr(HRESULT error, const char* operation) { if (FAILED(error)) fail(operation, error); }

struct SuiteReference { const char* name = nullptr; int version = 0; bool acquired = false; };
class CaptureContext {
public:
    explicit CaptureContext(SPBasicSuite* value) : basic(value) {}
    ~CaptureContext() { close(); }
    CaptureContext(const CaptureContext&) = delete;
    CaptureContext& operator=(const CaptureContext&) = delete;

    template<class T> const T* acquire(const char* name, int version) {
        auto& reference = suites_[count_++];
        reference.name = name; reference.version = version;
        const void* table = nullptr;
        const auto error = basic->AcquireSuite(name, version, &table);
        reference.acquired = error == kSPNoError;
        if (error != kSPNoError || !table) fail("无法取得 AE 截图接口", error);
        return static_cast<const T*>(table);
    }
    void close() noexcept {
        // Non-null out handles are adopted even if their SDK call returned an
        // error, so partial construction also follows the documented cleanup.
        if (name_handle && name_locked) {
            name_locked = false;
            try { cleanup_error(memory->AEGP_UnlockMemHandle(name_handle)); } catch (...) { cleanup_error(-1); }
        }
        if (name_handle) {
            const auto value = name_handle; name_handle = nullptr;
            try { cleanup_error(memory->AEGP_FreeMemHandle(value)); } catch (...) { cleanup_error(-1); }
        }
        if (owned_world) {
            const auto value = owned_world; owned_world = nullptr;
            try { cleanup_error(world->AEGP_Dispose(value)); } catch (...) { cleanup_error(-1); }
        }
        if (profile) {
            const auto value = profile; profile = nullptr;
            try { cleanup_error(color->AEGP_DisposeColorProfile(value)); } catch (...) { cleanup_error(-1); }
        }
        if (receipt) {
            const auto value = receipt; receipt = nullptr;
            try { cleanup_error(render->AEGP_CheckinFrame(value)); } catch (...) { cleanup_error(-1); }
        }
        if (options_handle) {
            const auto value = options_handle; options_handle = nullptr;
            try { cleanup_error(options->AEGP_Dispose(value)); } catch (...) { cleanup_error(-1); }
        }
        while (count_) {
            auto& reference = suites_[--count_];
            if (!reference.acquired) continue;
            reference.acquired = false;
            try { cleanup_error(basic->ReleaseSuite(reference.name, reference.version)); }
            catch (...) { cleanup_error(-1); }
        }
    }
    A_Err cleanup_failure = A_Err_NONE;
    SPBasicSuite* basic;
    const AEGP_ItemSuite8* item = nullptr;
    const AEGP_LayerSuite8* layer = nullptr;
    const AEGP_CompSuite7* comp = nullptr;
    const AEGP_RenderOptionsSuite3* options = nullptr;
    const AEGP_RenderSuite3* render = nullptr;
    const AEGP_WorldSuite3* world = nullptr;
    const AEGP_ProjSuite5* project = nullptr;
    const AEGP_ColorSettingsSuite2* color = nullptr;
    const AEGP_MemorySuite1* memory = nullptr;
    AEGP_RenderOptionsH options_handle = nullptr;
    AEGP_FrameReceiptH receipt = nullptr;
    AEGP_WorldH owned_world = nullptr;
    AEGP_ColorProfileP profile = nullptr;
    AEGP_MemHandle name_handle = nullptr;
    bool name_locked = false;
private:
    void cleanup_error(A_Err error) noexcept { if (!cleanup_failure && error) cleanup_failure = error; }
    std::array<SuiteReference, 8> suites_{};
    size_t count_ = 0;
};

void acquire_suites(CaptureContext& context) {
    context.item = context.acquire<AEGP_ItemSuite8>(kAEGPItemSuite, kAEGPItemSuiteVersion8);
    context.layer = context.acquire<AEGP_LayerSuite8>(kAEGPLayerSuite, kAEGPLayerSuiteVersion8);
    context.comp = context.acquire<AEGP_CompSuite7>(kAEGPCompSuite, kAEGPCompSuiteVersion7);
    context.options = context.acquire<AEGP_RenderOptionsSuite3>(kAEGPRenderOptionsSuite, kAEGPRenderOptionsSuiteVersion3);
    context.render = context.acquire<AEGP_RenderSuite3>(kAEGPRenderSuite, kAEGPRenderSuiteVersion3);
    context.world = context.acquire<AEGP_WorldSuite3>(kAEGPWorldSuite, kAEGPWorldSuiteVersion3);
    context.project = context.acquire<AEGP_ProjSuite5>(kAEGPProjSuite, kAEGPProjSuiteVersion5);
    context.color = context.acquire<AEGP_ColorSettingsSuite2>(kAEGPColorSettingsSuite, kAEGPColorSettingsSuiteVersion2);
    if (!context.item->AEGP_GetActiveItem || !context.item->AEGP_GetItemType ||
        !context.item->AEGP_GetItemCurrentTime || !context.item->AEGP_GetItemPixelAspectRatio ||
        !context.item->AEGP_GetItemMRUView ||
        !context.layer->AEGP_GetActiveLayer || !context.layer->AEGP_GetLayerObjectType ||
        !context.layer->AEGP_GetLayerParentComp || !context.layer->AEGP_GetLayerCurrentTime ||
        !context.comp->AEGP_GetItemFromComp || !context.comp->AEGP_GetCompFromItem ||
        !context.options->AEGP_NewFromItem || !context.options->AEGP_Dispose ||
        !context.options->AEGP_SetTime || !context.options->AEGP_SetWorldType ||
        !context.options->AEGP_SetMatteMode || !context.options->AEGP_SetChannelOrder ||
        !context.options->AEGP_SetDownsampleFactor || !context.render->AEGP_RenderAndCheckoutFrame ||
        !context.render->AEGP_CheckinFrame || !context.render->AEGP_GetReceiptWorld ||
        !context.world->AEGP_GetType || !context.world->AEGP_GetSize ||
        !context.world->AEGP_GetRowBytes || !context.world->AEGP_GetBaseAddr8 ||
        !context.world->AEGP_GetBaseAddr16 || !context.world->AEGP_GetBaseAddr32 ||
        !context.world->AEGP_New || !context.world->AEGP_Dispose ||
        !context.project->AEGP_GetProjectByIndex || !context.project->AEGP_GetProjectBitDepth ||
        !context.color->AEGP_DoesViewHaveColorSpaceXform ||
        !context.color->AEGP_GetNewWorkingSpaceColorProfile || !context.color->AEGP_DisposeColorProfile ||
        !context.color->AEGP_XformWorkingToViewColorSpace) {
        fail("AE 截图接口不完整");
    }
}

void acquire_name_suites(CaptureContext& context) {
    context.item = context.acquire<AEGP_ItemSuite8>(kAEGPItemSuite, kAEGPItemSuiteVersion8);
    context.layer = context.acquire<AEGP_LayerSuite8>(kAEGPLayerSuite, kAEGPLayerSuiteVersion8);
    context.comp = context.acquire<AEGP_CompSuite7>(kAEGPCompSuite, kAEGPCompSuiteVersion7);
    context.memory = context.acquire<AEGP_MemorySuite1>(kAEGPMemorySuite, kAEGPMemorySuiteVersion1);
    if (!context.item->AEGP_GetActiveItem || !context.item->AEGP_GetItemType || !context.item->AEGP_GetItemName ||
        !context.layer->AEGP_GetActiveLayer || !context.layer->AEGP_GetLayerObjectType || !context.layer->AEGP_GetLayerParentComp ||
        !context.comp->AEGP_GetItemFromComp || !context.memory->AEGP_FreeMemHandle || !context.memory->AEGP_LockMemHandle ||
        !context.memory->AEGP_UnlockMemHandle || !context.memory->AEGP_GetMemHandleSize) {
        fail("AE 合成名称接口不完整");
    }
}

struct ActiveComp { AEGP_ItemH item = nullptr; AEGP_CompH comp = nullptr; AEGP_LayerH layer = nullptr; };
ActiveComp active_comp_item(CaptureContext& context) {
    ActiveComp result;
    AEGP_ItemH item = nullptr;
    AEGP_LayerH layer = nullptr;
    check_ae(context.layer->AEGP_GetActiveLayer(&layer), "无法取得当前图层");
    if (layer) {
        AEGP_ObjectType type = AEGP_ObjectType_NONE;
        check_ae(context.layer->AEGP_GetLayerObjectType(layer, &type), "无法读取当前图层类型");
        if (type == AEGP_ObjectType_AV || type == AEGP_ObjectType_TEXT || type == AEGP_ObjectType_VECTOR) {
            AEGP_CompH comp = nullptr;
            check_ae(context.layer->AEGP_GetLayerParentComp(layer, &comp), "无法取得图层所属合成");
            if (!comp) fail("当前图层没有可截图的合成");
            check_ae(context.comp->AEGP_GetItemFromComp(comp, &item), "无法取得当前合成项目");
            if (!item) fail("当前合成项目为空");
            result.comp = comp; result.layer = layer;
        }
    }
    if (!item) check_ae(context.item->AEGP_GetActiveItem(&item), "无法取得当前项目");
    if (!item) fail("请先激活一个合成再截图");
    AEGP_ItemType type = AEGP_ItemType_NONE;
    check_ae(context.item->AEGP_GetItemType(item, &type), "无法读取当前项目类型");
    if (type != AEGP_ItemType_COMP) fail("请先激活一个合成再截图");
    result.item = item;
    return result;
}

struct Pixels { UINT width = 0, height = 0, corrected_width = 0; std::vector<BYTE> bgra; };
AEGP_WorldType project_world_type(CaptureContext& context) {
    AEGP_ProjectH project = nullptr;
    check_ae(context.project->AEGP_GetProjectByIndex(0, &project), "无法取得当前工程");
    if (!project) fail("请先打开工程再截图");
    AEGP_ProjBitDepth depth = AEGP_ProjBitDepth_8;
    check_ae(context.project->AEGP_GetProjectBitDepth(project, &depth), "无法读取工程像素深度");
    switch (depth) {
    case AEGP_ProjBitDepth_8: return AEGP_WorldType_8;
    case AEGP_ProjBitDepth_16: return AEGP_WorldType_16;
    case AEGP_ProjBitDepth_32: return AEGP_WorldType_32;
    default: fail("工程像素深度不受支持");
    }
}
struct WorldPixels {
    A_long width = 0, height = 0;
    A_u_long stride = 0;
    size_t pixel_bytes = 0;
    BYTE* base = nullptr;
};
WorldPixels read_world(CaptureContext& context, AEGP_WorldH world, AEGP_WorldType expected) {
    AEGP_WorldType type = AEGP_WorldType_NONE;
    check_ae(context.world->AEGP_GetType(world, &type), "无法读取截图像素类型");
    if (type != expected) fail("AE 返回了不匹配的截图像素类型");
    WorldPixels value;
    value.pixel_bytes = type == AEGP_WorldType_8 ? sizeof(PF_Pixel8) :
        type == AEGP_WorldType_16 ? sizeof(PF_Pixel16) : sizeof(PF_PixelFloat);
    check_ae(context.world->AEGP_GetSize(world, &value.width, &value.height), "无法读取截图尺寸");
    if (value.width <= 0 || value.height <= 0) fail("AE 返回了无效截图尺寸");
    check_ae(context.world->AEGP_GetRowBytes(world, &value.stride), "无法读取截图行跨度");
    const auto row = static_cast<std::uint64_t>(value.width) * value.pixel_bytes;
    const auto extent = static_cast<std::uint64_t>(value.height - 1) * value.stride + row;
    if (row > value.stride || extent > (std::numeric_limits<size_t>::max)())
        fail("截图尺寸或像素地址范围无效");
    if (type == AEGP_WorldType_8) {
        PF_Pixel8* base = nullptr;
        check_ae(context.world->AEGP_GetBaseAddr8(world, &base), "无法读取截图像素地址");
        value.base = reinterpret_cast<BYTE*>(base);
    } else if (type == AEGP_WorldType_16) {
        PF_Pixel16* base = nullptr;
        check_ae(context.world->AEGP_GetBaseAddr16(world, &base), "无法读取截图像素地址");
        value.base = reinterpret_cast<BYTE*>(base);
    } else {
        PF_PixelFloat* base = nullptr;
        check_ae(context.world->AEGP_GetBaseAddr32(world, &base), "无法读取截图像素地址");
        value.base = reinterpret_cast<BYTE*>(base);
    }
    if (!value.base) fail("AE 返回了空截图像素地址");
    return value;
}
void quantize_pixel(const BYTE* source, AEGP_WorldType type, BYTE* destination) {
    __m128 samples;
    if (type == AEGP_WorldType_8) {
        PF_Pixel8 pixel{}; std::memcpy(&pixel, source, sizeof(pixel));
        samples = _mm_mul_ps(_mm_setr_ps(static_cast<float>(pixel.alpha), static_cast<float>(pixel.red),
            static_cast<float>(pixel.green), static_cast<float>(pixel.blue)), _mm_set1_ps(1.0f / 255.0f));
    } else if (type == AEGP_WorldType_16) {
        PF_Pixel16 pixel{}; std::memcpy(&pixel, source, sizeof(pixel));
        // Original worker 0x180063070 uses these exact float bits, rather
        // than the SDK white value 32768 as a divisor.
        samples = _mm_mul_ps(_mm_setr_ps(static_cast<float>(pixel.alpha), static_cast<float>(pixel.red),
            static_cast<float>(pixel.green), static_cast<float>(pixel.blue)),
            _mm_set1_ps(std::bit_cast<float>(std::uint32_t{0x38000100})));
    } else {
        PF_PixelFloat pixel{}; std::memcpy(&pixel, source, sizeof(pixel));
        samples = _mm_setr_ps(pixel.alpha, pixel.red, pixel.green, pixel.blue);
    }
    // Match MULPS/CVTPS2DQ/PACKSSDW/PACKUSWB, including MXCSR rounding,
    // nonfinite/overflow conversion and signed-then-unsigned saturation.
    const auto words = _mm_cvtps_epi32(_mm_mul_ps(samples, _mm_set1_ps(255.0f)));
    const auto shorts = _mm_packs_epi32(words, words);
    const auto bytes = _mm_packus_epi16(shorts, shorts);
    const auto argb = static_cast<std::uint32_t>(_mm_cvtsi128_si32(bytes));
    destination[0] = static_cast<BYTE>(argb >> 24);
    destination[1] = static_cast<BYTE>(argb >> 16);
    destination[2] = static_cast<BYTE>(argb >> 8);
    destination[3] = static_cast<BYTE>(argb);
}
Pixels render_pixels(CaptureContext& context, AEGP_PluginID plugin, bool full_resolution) {
    auto active = active_comp_item(context);
    const auto item = active.item;
    const auto world_type = project_world_type(context);
    A_Time time{};
    if (active.layer)
        check_ae(context.layer->AEGP_GetLayerCurrentTime(active.layer, AEGP_LTimeMode_CompTime, &time),
            "无法读取活动图层合成时间");
    else check_ae(context.item->AEGP_GetItemCurrentTime(item, &time), "无法读取合成当前时间");
    if (!time.scale) fail("合成当前时间无效");
    A_Ratio aspect{};
    check_ae(context.item->AEGP_GetItemPixelAspectRatio(item, &aspect), "无法读取合成像素宽高比");
    if (aspect.num <= 0 || !aspect.den) fail("合成像素宽高比无效");

    check_ae(context.options->AEGP_NewFromItem(plugin, item, &context.options_handle), "无法创建截图渲染选项");
    if (!context.options_handle) fail("AE 返回了空截图渲染选项");
    check_ae(context.options->AEGP_SetTime(context.options_handle, time), "无法设置截图时间");
    check_ae(context.options->AEGP_SetWorldType(context.options_handle, world_type), "无法设置截图像素深度");
    // Explicit SDK modes avoid assuming how NewFromItem handles alpha/channel
    // defaults. PF_Pixel8 is alpha/red/green/blue; no premultiplication is done.
    check_ae(context.options->AEGP_SetMatteMode(context.options_handle, AEGP_MatteMode_STRAIGHT), "无法设置截图透明度模式");
    check_ae(context.options->AEGP_SetChannelOrder(context.options_handle, AEGP_ChannelOrder_ARGB), "无法设置截图通道顺序");
    if (full_resolution) check_ae(context.options->AEGP_SetDownsampleFactor(context.options_handle, 1, 1),
        "无法设置全分辨率截图");
    check_ae(context.render->AEGP_RenderAndCheckoutFrame(context.options_handle, nullptr, nullptr, &context.receipt),
        "AE 截图渲染失败或已取消");
    if (!context.receipt) fail("AE 没有返回截图帧");
    AEGP_WorldH world = nullptr;
    check_ae(context.render->AEGP_GetReceiptWorld(context.receipt, &world), "无法取得截图像素");
    if (!world) fail("AE 返回了空截图像素");
    const auto source = read_world(context, world, world_type);
    const auto width = source.width, height = source.height;
    const auto stride = static_cast<std::uint64_t>(width) * 4;
    const auto bytes = stride * static_cast<std::uint64_t>(height);
    const auto corrected = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(aspect.num) / aspect.den;
    if (bytes > (std::numeric_limits<UINT>::max)() ||
        bytes > (std::numeric_limits<size_t>::max)() || !corrected ||
        corrected > (std::numeric_limits<UINT>::max)() / 4 ||
        corrected * static_cast<std::uint64_t>(height) * 4 > (std::numeric_limits<UINT>::max)()) {
        fail("截图尺寸、行跨度或像素宽高比超出可用范围");
    }
    AEGP_ItemViewP view = nullptr;
    check_ae(context.item->AEGP_GetItemMRUView(item, &view), "无法读取截图视图");
    if (!view) fail("请先打开合成视图再截图");
    A_Boolean has_transform = FALSE;
    check_ae(context.color->AEGP_DoesViewHaveColorSpaceXform(view, &has_transform), "无法读取视图颜色转换");
    auto output_world = source;
    if (has_transform) {
        if (!active.comp) {
            check_ae(context.comp->AEGP_GetCompFromItem(item, &active.comp), "无法取得截图合成");
            if (!active.comp) fail("截图合成为空");
        }
        check_ae(context.color->AEGP_GetNewWorkingSpaceColorProfile(plugin, active.comp, &context.profile),
            "无法读取截图工作色彩空间");
        if (!context.profile) fail("AE 返回了空截图工作色彩空间");
        check_ae(context.world->AEGP_New(plugin, world_type, width, height, &context.owned_world),
            "无法创建截图颜色转换图像");
        if (!context.owned_world) fail("AE 返回了空截图颜色转换图像");
        output_world = read_world(context, context.owned_world, world_type);
        if (output_world.width != width || output_world.height != height) fail("截图颜色转换图像尺寸不匹配");
        for (A_long y = 0; y < height; ++y)
            std::memcpy(output_world.base + static_cast<size_t>(y) * output_world.stride,
                source.base + static_cast<size_t>(y) * source.stride,
                static_cast<size_t>(width) * source.pixel_bytes);
        check_ae(context.color->AEGP_XformWorkingToViewColorSpace(view, world, context.owned_world),
            "截图视图颜色转换失败");
        for (A_long y = 0; y < height; ++y) for (A_long x = 0; x < width; ++x)
            std::memcpy(output_world.base + static_cast<size_t>(y) * output_world.stride +
                static_cast<size_t>(x) * source.pixel_bytes,
                source.base + static_cast<size_t>(y) * source.stride +
                static_cast<size_t>(x) * source.pixel_bytes, source.pixel_bytes / 4);
    }
    Pixels result{static_cast<UINT>(width), static_cast<UINT>(height), static_cast<UINT>(corrected), {}};
    result.bgra.resize(static_cast<size_t>(bytes));
    for (A_long y = 0; y < height; ++y) {
        const auto* row = output_world.base + static_cast<size_t>(y) * output_world.stride;
        auto* destination = result.bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(stride);
        for (A_long x = 0; x < width; ++x) {
            quantize_pixel(row + static_cast<size_t>(x) * source.pixel_bytes,
                world_type, destination + static_cast<size_t>(x) * 4);
        }
    }
    return result;
}

class ComScope {
public:
    ComScope() {
        const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (result != RPC_E_CHANGED_MODE) check_hr(result, "无法初始化截图图像接口");
        owned_ = SUCCEEDED(result);
    }
    ~ComScope() { if (owned_) CoUninitialize(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    bool owned_ = false;
};

std::wstring extended_path(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal().wstring();
    if (absolute.rfind(L"\\\\?\\", 0) == 0) return absolute;
    if (absolute.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + absolute.substr(2);
    return L"\\\\?\\" + absolute;
}
class TemporaryFile {
public:
    explicit TemporaryFile(std::wstring value) : path(std::move(value)) {}
    ~TemporaryFile() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (created) DeleteFileW(path.c_str());
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    std::wstring path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool created = false;
};
void publish_stream(IStream* stream, const std::filesystem::path& target) {
    GUID id{}; check_hr(CoCreateGuid(&id), "无法创建截图临时文件名");
    wchar_t suffix[40]{};
    if (!StringFromGUID2(id, suffix, static_cast<int>(std::size(suffix)))) fail("无法创建截图临时文件名");
    const auto temporary = target.parent_path() / (std::wstring(L".xfx-") + suffix + L".tmp");
    TemporaryFile file(extended_path(temporary));
    file.handle = CreateFileW(file.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE) fail("无法创建截图文件", GetLastError());
    file.created = true;
    STATSTG description{}; check_hr(stream->Stat(&description, STATFLAG_NONAME), "无法读取截图编码长度");
    if (!description.cbSize.QuadPart) fail("截图编码结果为空");
    LARGE_INTEGER zero{}; check_hr(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "无法读取截图编码数据");
    auto remaining = description.cbSize.QuadPart;
    std::array<BYTE, 64 * 1024> block{};
    while (remaining) {
        const ULONG count = static_cast<ULONG>((std::min)(remaining, static_cast<ULONGLONG>(block.size())));
        ULONG read = 0; check_hr(stream->Read(block.data(), count, &read), "无法读取截图编码数据");
        if (read != count) fail("截图编码数据不完整");
        DWORD written = 0;
        if (!WriteFile(file.handle, block.data(), count, &written, nullptr) || written != count)
            fail("写入截图文件失败", GetLastError());
        remaining -= count;
    }
    if (!FlushFileBuffers(file.handle)) fail("保存截图文件失败", GetLastError());
    const HANDLE handle = file.handle; file.handle = INVALID_HANDLE_VALUE;
    if (!CloseHandle(handle)) fail("关闭截图文件失败", GetLastError());
    const auto destination = extended_path(target);
    if (!MoveFileExW(file.path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fail("无法替换截图文件", GetLastError());
    file.created = false;
}

void write_png(Pixels& pixels, const std::filesystem::path& target) {
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
        "无法创建 PNG 编码接口");
    ComPtr<IWICBitmap> bitmap;
    check_hr(factory->CreateBitmapFromMemory(pixels.width, pixels.height, GUID_WICPixelFormat32bppBGRA,
        pixels.width * 4, static_cast<UINT>(pixels.bgra.size()), pixels.bgra.data(), &bitmap), "无法创建截图图像");
    ComPtr<IWICBitmapSource> source = bitmap;
    ComPtr<IWICBitmapScaler> scaler;
    if (pixels.corrected_width != pixels.width) {
        check_hr(factory->CreateBitmapScaler(&scaler), "无法创建像素宽高比校正接口");
        check_hr(scaler->Initialize(bitmap.Get(), pixels.corrected_width, pixels.height, WICBitmapInterpolationModeFant),
            "无法校正截图像素宽高比");
        source = scaler;
    }
    ComPtr<IStream> encoded; check_hr(CreateStreamOnHGlobal(nullptr, TRUE, &encoded), "无法创建 PNG 编码缓冲区");
    ComPtr<IWICBitmapEncoder> encoder;
    check_hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "无法创建 PNG 编码器");
    check_hr(encoder->Initialize(encoded.Get(), WICBitmapEncoderNoCache), "无法初始化 PNG 编码器");
    ComPtr<IWICBitmapFrameEncode> frame;
    check_hr(encoder->CreateNewFrame(&frame, nullptr), "无法创建 PNG 图像帧");
    check_hr(frame->Initialize(nullptr), "无法初始化 PNG 图像帧");
    check_hr(frame->SetSize(pixels.corrected_width, pixels.height), "无法设置 PNG 图像尺寸");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check_hr(frame->SetPixelFormat(&format), "无法设置 PNG 透明像素格式");
    ComPtr<IWICFormatConverter> converted;
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        // The PNG encoder may negotiate RGBA. Convert explicitly rather than
        // relabeling BGRA bytes or accepting a format that drops alpha.
        if (!IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA)) fail("PNG 编码器没有透明像素格式");
        check_hr(factory->CreateFormatConverter(&converted), "无法创建 PNG 像素转换接口");
        check_hr(converted->Initialize(source.Get(), format, WICBitmapDitherTypeNone, nullptr, 0,
            WICBitmapPaletteTypeCustom), "无法转换 PNG 像素格式");
        source = converted;
    }
    check_hr(frame->WriteSource(source.Get(), nullptr), "PNG 图像编码失败");
    check_hr(frame->Commit(), "PNG 图像帧保存失败");
    check_hr(encoder->Commit(), "PNG 图像保存失败");
    publish_stream(encoded.Get(), target);
}
} // namespace

CaptureCompName current_capture_comp_name(SPBasicSuite* basic, AEGP_PluginID plugin_id) {
    CaptureCompName result;
    try {
        if (!basic || !basic->AcquireSuite || !basic->ReleaseSuite) fail("AE 合成名称接口不可用");
        CaptureContext context(basic);
        acquire_name_suites(context);
        const auto active = active_comp_item(context);
        check_ae(context.item->AEGP_GetItemName(plugin_id, active.item, &context.name_handle), "无法读取当前合成名称");
        if (!context.name_handle) fail("AE 返回了空的合成名称句柄");
        AEGP_MemSize bytes = 0;
        check_ae(context.memory->AEGP_GetMemHandleSize(context.name_handle, &bytes), "无法读取合成名称长度");
        if (bytes < sizeof(A_UTF16Char) || bytes % sizeof(A_UTF16Char)) fail("AE 返回了无效合成名称长度");
        void* pointer = nullptr;
        check_ae(context.memory->AEGP_LockMemHandle(context.name_handle, &pointer), "无法锁定合成名称");
        context.name_locked = true;
        if (!pointer) fail("AE 返回了空的合成名称地址");
        const auto* first = static_cast<const A_UTF16Char*>(pointer);
        const auto* limit = first + bytes / sizeof(A_UTF16Char);
        const auto* last = std::find(first, limit, static_cast<A_UTF16Char>(0));
        if (last == limit) fail("AE 合成名称缺少结束符");
        std::wstring name(first, last);
        context.close();
        if (context.cleanup_failure) fail("AE 合成名称资源释放失败", context.cleanup_failure);
        result.name = std::move(name);
        result.reply.succeeded = true; result.reply.count = 1;
    } catch (const std::bad_alloc&) {
        result.reply.message = "读取合成名称失败：可用内存不足";
    } catch (const std::exception& error) {
        result.reply.message = error.what();
    } catch (...) {
        result.reply.message = "读取合成名称失败：未知错误";
    }
    return result;
}

ScriptReply capture_comp_png(SPBasicSuite* basic, AEGP_PluginID plugin_id,
                             const std::filesystem::path& target, bool full_resolution) {
    ScriptReply result;
    try {
        if (!basic || !basic->AcquireSuite || !basic->ReleaseSuite) fail("AE 截图接口不可用");
        if (target.empty() || target.filename().empty() || target.native().find(L'\0') != std::wstring::npos)
            fail("截图保存路径无效");
        const auto attributes = GetFileAttributesW(extended_path(target).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            fail("截图保存路径必须是文件");
        CaptureContext context(basic);
        acquire_suites(context);
        auto pixels = render_pixels(context, plugin_id, full_resolution);
        context.close();
        if (context.cleanup_failure) fail("AE 截图资源释放失败", context.cleanup_failure);
        write_png(pixels, target);
        result.succeeded = true; result.count = 1;
    } catch (const std::bad_alloc&) {
        result.message = "截图失败：可用内存不足";
    } catch (const std::filesystem::filesystem_error& error) {
        result.message = "截图失败：文件系统错误 (" + std::to_string(error.code().value()) + ")";
    } catch (const std::exception& error) {
        result.message = error.what();
    } catch (...) {
        result.message = "截图失败：未知错误";
    }
    return result;
}
} // namespace xfx
