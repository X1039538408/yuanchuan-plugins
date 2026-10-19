#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace xfx {

struct GalleryRect {
    double x = 0, y = 0, width = 0, height = 0;
};
struct GallerySource {
    std::filesystem::path path;
    unsigned width = 0, height = 0;
};
struct GalleryView {
    GallerySource source;
    GalleryRect cell;
    double scale = 1, offset_x = 0, offset_y = 0;
    // Rebuild snapshot used by Z; later zoom, pan and resize leave it unchanged.
    double saved_scale = 1, saved_offset_x = 0, saved_offset_y = 0;
};

// Geometry/state only. This model does not define mouse gestures, selection
// priority beyond the caller's order, or keyboard auto-repeat handling.
class GalleryModel {
public:
    void set_images(const std::vector<GallerySource>& sources);
    void set_bounds(GalleryRect bounds);
    void toggle_direction();
    void toggle_actual_size();
    void set_offset(std::size_t index, double x, double y);
    // Multiplicative zoom anchored in the same gallery coordinates as image_rect.
    void zoom_at(std::size_t index, double factor, double anchor_x, double anchor_y);
    const std::vector<GalleryView>& views() const noexcept;
    GalleryRect image_rect(std::size_t index) const noexcept;
    bool horizontal() const noexcept;
    // Current scale fact, not a mode that overrides resize/fit constraints.
    bool actual_size() const noexcept;
private:
    void relayout();
    static double fit_scale(const GalleryView& view) noexcept;
    static void constrain(GalleryView& view) noexcept;
    GalleryRect bounds_;
    std::vector<GalleryView> views_;
    bool horizontal_ = false;
    bool row_major_ = false;
};

} // namespace xfx
