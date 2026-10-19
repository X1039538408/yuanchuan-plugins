#include "gallery_model.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace xfx {
namespace {

double finite_or_zero(double value) noexcept { return std::isfinite(value) ? value : 0; }
double dimension(double value) noexcept { return (std::max)(0.0, finite_or_zero(value)); }
double add(double first, double second) noexcept {
    const double value = first + second;
    return std::isfinite(value) ? value : std::copysign((std::numeric_limits<double>::max)(), value);
}
double multiply(double first, double second) noexcept {
    const double value = first * second;
    return std::isfinite(value) ? value : std::copysign((std::numeric_limits<double>::max)(), value);
}
bool valid_source(const GallerySource& source) noexcept { return source.width != 0 && source.height != 0; }
double center(double viewport, double display) noexcept { return std::trunc((viewport - display) / 2); }
double clamp_offset(double viewport, double display, double offset) noexcept {
    if (display <= viewport) return 0;
    const double centered = center(viewport, display);
    return std::clamp(offset, viewport - display - centered, -centered);
}
double position(double viewport, double display, double offset) noexcept {
    const double centered = center(viewport, display);
    if (display <= viewport) return centered;
    return std::clamp(add(centered, offset), viewport - display, 0.0);
}

} // namespace

double GalleryModel::fit_scale(const GalleryView& view) noexcept {
    if (!valid_source(view.source)) return 1;
    return (std::min)(view.cell.width / view.source.width, view.cell.height / view.source.height);
}

void GalleryModel::constrain(GalleryView& view) noexcept {
    if (!valid_source(view.source)) {
        view.scale = 1;
        view.offset_x = view.offset_y = 0;
        return;
    }
    const double largest_dimension = static_cast<double>((std::max)(view.source.width, view.source.height));
    const double largest_scale = (std::numeric_limits<double>::max)() / largest_dimension;
    view.scale = (std::max)(fit_scale(view), (std::min)(dimension(view.scale), largest_scale));
    const double width = multiply(view.source.width, view.scale);
    const double height = multiply(view.source.height, view.scale);
    view.offset_x = clamp_offset(view.cell.width, width, finite_or_zero(view.offset_x));
    view.offset_y = clamp_offset(view.cell.height, height, finite_or_zero(view.offset_y));
}

void GalleryModel::relayout() {
    if (views_.empty()) return;
    std::size_t columns = 1, rows = 1;
    if (views_.size() == 2) {
        columns = horizontal_ ? 2 : 1;
        rows = horizontal_ ? 1 : 2;
    } else if (views_.size() > 2) { columns = rows = 2; }
    const double gap_x = columns > 1 ? (std::min)(2.0, bounds_.width) : 0;
    const double gap_y = rows > 1 ? (std::min)(2.0, bounds_.height) : 0;
    const double cell_width = (bounds_.width - gap_x * static_cast<double>(columns - 1)) / static_cast<double>(columns);
    const double cell_height = (bounds_.height - gap_y * static_cast<double>(rows - 1)) / static_cast<double>(rows);
    for (std::size_t i = 0; i < views_.size(); ++i) {
        const std::size_t column = row_major_ ? i % columns : i / rows;
        const std::size_t row = row_major_ ? i / columns : i % rows;
        views_[i].cell = {add(bounds_.x, (cell_width + gap_x) * static_cast<double>(column)),
                         add(bounds_.y, (cell_height + gap_y) * static_cast<double>(row)), cell_width, cell_height};
        constrain(views_[i]);
    }
}

void GalleryModel::set_images(const std::vector<GallerySource>& sources) {
    std::vector<GalleryView> rebuilt;
    const std::size_t count = (std::min)(std::size_t{4}, sources.size());
    rebuilt.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        GalleryView view;
        view.source = sources[i];
        view.scale = valid_source(view.source) ? 0 : 1;
        rebuilt.push_back(std::move(view));
    }
    views_ = std::move(rebuilt);
    row_major_ = horizontal_;
    relayout();
    // Z restores the rebuild snapshot; wheel zoom and pan do not overwrite it.
    for (auto& view : views_) {
        view.saved_scale = view.scale;
        view.saved_offset_x = view.offset_x;
        view.saved_offset_y = view.offset_y;
    }
}

void GalleryModel::set_bounds(GalleryRect bounds) {
    bounds.x = finite_or_zero(bounds.x);
    bounds.y = finite_or_zero(bounds.y);
    bounds.width = dimension(bounds.width);
    bounds.height = dimension(bounds.height);
    if (views_.size() > 2 && (bounds.width != bounds_.width || bounds.height != bounds_.height)) {
        // The reference resize branch fills rows, even with the default A flag.
        row_major_ = true;
    }
    bounds_ = bounds;
    relayout(); // constrain uses max(current scale, fit); resize never zooms down.
}

void GalleryModel::toggle_direction() {
    std::vector<GallerySource> sources;
    sources.reserve(views_.size());
    for (const auto& view : views_) sources.push_back(view.source);
    horizontal_ = !horizontal_;
    set_images(sources); // A reconstructs previews, including their Z snapshot.
}

void GalleryModel::toggle_actual_size() {
    const bool any_not_one = std::any_of(views_.begin(), views_.end(), [](const GalleryView& view) { return view.scale != 1; });
    for (auto& view : views_) {
        view.scale = any_not_one ? 1 : view.saved_scale;
        view.offset_x = any_not_one ? 0 : view.saved_offset_x;
        view.offset_y = any_not_one ? 0 : view.saved_offset_y;
        constrain(view);
    }
}

void GalleryModel::set_offset(std::size_t index, double x, double y) {
    if (index >= views_.size() || !std::isfinite(x) || !std::isfinite(y)) return;
    views_[index].offset_x = x;
    views_[index].offset_y = y;
    constrain(views_[index]);
}

void GalleryModel::zoom_at(std::size_t index, double factor, double anchor_x, double anchor_y) {
    if (index >= views_.size() || !std::isfinite(factor) || factor <= 0 ||
        !std::isfinite(anchor_x) || !std::isfinite(anchor_y) || !valid_source(views_[index].source)) return;
    auto& view = views_[index];
    const GalleryRect previous = image_rect(index);
    const double previous_scale = view.scale;
    view.scale = multiply(view.scale, factor);
    constrain(view);
    if (previous_scale <= 0) return;
    double ratio = view.scale / previous_scale;
    if (!std::isfinite(ratio)) ratio = (std::numeric_limits<double>::max)();
    const double new_width = multiply(view.source.width, view.scale);
    const double new_height = multiply(view.source.height, view.scale);
    const double left = add(anchor_x, -multiply(add(anchor_x, -previous.x), ratio));
    const double top = add(anchor_y, -multiply(add(anchor_y, -previous.y), ratio));
    view.offset_x = add(left, -add(view.cell.x, center(view.cell.width, new_width)));
    view.offset_y = add(top, -add(view.cell.y, center(view.cell.height, new_height)));
    constrain(view);
}

const std::vector<GalleryView>& GalleryModel::views() const noexcept { return views_; }
GalleryRect GalleryModel::image_rect(std::size_t index) const noexcept {
    if (index >= views_.size()) return {};
    const auto& view = views_[index];
    if (!valid_source(view.source)) return {view.cell.x, view.cell.y, 0, 0};
    const double width = multiply(view.source.width, view.scale);
    const double height = multiply(view.source.height, view.scale);
    return {add(view.cell.x, position(view.cell.width, width, view.offset_x)),
            add(view.cell.y, position(view.cell.height, height, view.offset_y)), width, height};
}
bool GalleryModel::horizontal() const noexcept { return horizontal_; }
bool GalleryModel::actual_size() const noexcept {
    bool valid = false;
    for (const auto& view : views_) {
        if (!valid_source(view.source)) continue;
        valid = true;
        if (view.scale != 1) return false;
    }
    return valid;
}

} // namespace xfx
