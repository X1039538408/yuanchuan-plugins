#include "gallery_model.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using xfx::GalleryModel;
using xfx::GalleryRect;
using xfx::GallerySource;

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void near(double actual, double expected, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 0.000001) throw std::runtime_error(message);
}
static void rect(GalleryRect actual, double x, double y, double width, double height) {
    near(actual.x, x, "rectangle x mismatch");
    near(actual.y, y, "rectangle y mismatch");
    near(actual.width, width, "rectangle width mismatch");
    near(actual.height, height, "rectangle height mismatch");
}
static void finite_rect(GalleryRect value) {
    require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) && std::isfinite(value.height), "rectangle must remain finite");
    require(value.width >= 0 && value.height >= 0, "rectangle has negative dimensions");
}
static std::vector<GallerySource> images(std::size_t count, unsigned width = 100, unsigned height = 50) {
    std::vector<GallerySource> result;
    for (std::size_t i = 0; i < count; ++i) result.push_back({std::to_string(i) + ".png", width, height});
    return result;
}

int main() {
    int failures = 0, checks = 0;
    const auto test = [&](const char* name, const std::function<void()>& action) {
        ++checks;
        try { action(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("empty model has safe geometry and no actual image size", [] {
        GalleryModel model;
        require(model.views().empty() && !model.horizontal() && !model.actual_size(), "initial state changed");
        model.set_bounds({10,20,202,102}); model.toggle_actual_size();
        require(!model.actual_size(), "empty Z must do nothing");
        rect(model.image_rect(0),0,0,0,0);
    });
    test("first four sources retain caller order and Unicode identities", [] {
        GalleryModel model;
        auto source = images(6);
        source[0].path = std::filesystem::path(u8"D:/中文🎬/比较.png");
        model.set_images(source);
        require(model.views().size() == 4, "comparison must contain at most four views");
        for (std::size_t i=0;i<4;++i) require(model.views()[i].source.path == source[i].path, "source identity/order changed");
        rect(model.image_rect(4),0,0,0,0);
    });
    test("one image fits down and records its initial fit snapshot", [] {
        GalleryModel model;
        model.set_bounds({10,20,100,100}); model.set_images(images(1,200,100));
        require(model.views().size() == 1, "one view required");
        rect(model.views()[0].cell,10,20,100,100);
        near(model.views()[0].scale,.5,"large image must fit down");
        near(model.views()[0].saved_scale,.5,"initial fit snapshot missing");
        rect(model.image_rect(0),10,45,100,50);
    });
    test("fit enlarges small images and remains a lower bound during Z", [] {
        GalleryModel model;
        model.set_bounds({10,20,100,80}); model.set_images(images(1,25,10));
        require(model.views().size() == 1, "small image view missing");
        near(model.views()[0].scale,4,"fit must not clamp small images to one");
        rect(model.image_rect(0),10,40,100,40);
        for (int i=0;i<2;++i) {
            model.toggle_actual_size();
            near(model.views()[0].scale,4,"Z must still respect fit lower bound");
            require(!model.actual_size(), "enlarged small image is not actually at one scale");
        }
    });
    test("two images default to vertical cells with two pixel gap", [] {
        GalleryModel model;
        model.set_bounds({10,20,202,202}); model.set_images(images(2));
        require(model.views().size() == 2 && !model.horizontal(), "default direction must be vertical");
        rect(model.views()[0].cell,10,20,202,100); rect(model.views()[1].cell,10,122,202,100);
        model.toggle_direction(); require(model.horizontal(), "A did not toggle direction");
        rect(model.views()[0].cell,10,20,100,202); rect(model.views()[1].cell,112,20,100,202);
        model.toggle_direction(); rect(model.views()[1].cell,10,122,202,100);
    });
    test("three and four images fill columns by default and rows after A", [] {
        for (const std::size_t count : {3u,4u}) {
            GalleryModel model;
            model.set_bounds({10,20,202,102}); model.set_images(images(count));
            require(model.views().size() == count, "comparison view count changed");
            rect(model.views()[0].cell,10,20,100,50); rect(model.views()[1].cell,10,72,100,50);
            rect(model.views()[2].cell,112,20,100,50);
            if (count == 4) rect(model.views()[3].cell,112,72,100,50);
            model.toggle_direction(); rect(model.views()[1].cell,112,20,100,50); rect(model.views()[2].cell,10,72,100,50);
        }
    });
    test("dimension resize uses rows without changing the direction preference", [] {
        for (const std::size_t count : {3u,4u}) {
            GalleryModel model;
            model.set_bounds({0,0,202,102}); model.set_images(images(count));
            require(model.views().size() == count, "resize comparison missing");
            model.set_bounds({10,20,202,102}); // Translation alone is not the size-change branch.
            rect(model.views()[1].cell,10,72,100,50);
            model.set_bounds({10,20,402,202});
            require(!model.horizontal(), "resize changed the saved A direction");
            rect(model.views()[1].cell,212,20,200,100); rect(model.views()[2].cell,10,122,200,100);
            model.set_images(images(count)); // Rebuild returns to the direction's column order.
            rect(model.views()[1].cell,10,122,200,100);
            model.set_bounds({10,20,402,202}); // Identical size does not force rows again.
            rect(model.views()[1].cell,10,122,200,100);
        }
    });
    test("fractional cells use truncated center positions", [] {
        GalleryModel model;
        model.set_bounds({11.5,13.5,205,105}); model.set_images(images(4,100,50));
        require(model.views().size() == 4, "fractional views missing");
        rect(model.views()[0].cell,11.5,13.5,101.5,51.5); rect(model.views()[3].cell,115,67,101.5,51.5);
        rect(model.image_rect(0),11.5,13.5,101.5,50.75);
    });
    test("resize raises scale to fit but never shrinks an existing scale", [] {
        GalleryModel model;
        model.set_bounds({0,0,100,100}); model.set_images(images(1,200,100));
        require(model.views().size() == 1, "resize view missing");
        model.set_bounds({10,20,400,300}); near(model.views()[0].scale,2,"larger resize did not fit up");
        rect(model.image_rect(0),10,70,400,200);
        model.set_bounds({10,20,100,100}); near(model.views()[0].scale,2,"smaller resize reduced current scale");
        rect(model.image_rect(0),-140,-30,400,200);
        near(model.views()[0].saved_scale,.5,"resize changed rebuild snapshot");
    });
    test("Z restores rebuild fit and zero pan rather than later user zoom", [] {
        GalleryModel model;
        model.set_bounds({0,0,100,100}); model.set_images(images(1,200,100));
        require(model.views().size() == 1, "Z view missing");
        model.zoom_at(0,4,50,50); model.set_offset(0,30,-10);
        near(model.views()[0].scale,2,"user zoom not applied");
        near(model.views()[0].saved_scale,.5,"user zoom overwrote rebuild snapshot");
        near(model.views()[0].saved_offset_x,0,"user pan overwrote rebuild snapshot");
        model.toggle_actual_size(); require(model.actual_size(), "Z did not reach one");
        near(model.views()[0].offset_x,0,"Z did not clear horizontal pan"); near(model.views()[0].offset_y,0,"Z did not clear vertical pan");
        model.toggle_actual_size(); near(model.views()[0].scale,.5,"second Z did not restore rebuild fit");
        near(model.views()[0].offset_x,0,"Z restored user pan rather than initial pan");
        require(!model.actual_size(), "restored fit is not one scale");
    });
    test("Z tests any scale and updates every valid comparison image", [] {
        GalleryModel model;
        model.set_bounds({0,0,202,202});
        model.set_images({{"wide.png",200,100},{"square.png",100,100},{"extra-wide.png",400,100}});
        require(model.views().size() == 3, "comparison Z views missing");
        const double fit[] = {.5,1,.25};
        model.toggle_actual_size(); require(model.actual_size(), "mixed scales did not all reach one");
        for (std::size_t i=0;i<3;++i) {
            near(model.views()[i].scale,1,"Z did not update every image");
            near(model.views()[i].saved_scale,fit[i],"Z altered rebuild snapshot");
        }
        rect(model.image_rect(0),-50,0,200,100);
        model.toggle_actual_size();
        for (std::size_t i=0;i<3;++i) near(model.views()[i].scale,fit[i],"Z did not restore individual rebuild fit");
    });
    test("Z restores rebuild scale after shrink and obeys a newer fit minimum", [] {
        GalleryModel model;
        model.set_bounds({0,0,400,400}); model.set_images(images(1,200,100));
        require(model.views().size() == 1, "resize Z view missing");
        model.set_bounds({0,0,100,100}); model.set_offset(0,30,-10);
        model.toggle_actual_size(); require(model.actual_size(), "shrunk Z did not reach one");
        model.toggle_actual_size(); near(model.views()[0].scale,2,"Z should restore initial scale, not current fit");
        near(model.views()[0].offset_x,0,"Z should restore initial zero offset");
        model.set_bounds({0,0,600,600}); near(model.views()[0].scale,3,"resize did not raise minimum fit");
        near(model.views()[0].saved_scale,2,"resize overwrote initial fit snapshot");
        model.toggle_actual_size(); near(model.views()[0].scale,3,"Z crossed the newer minimum fit");
        require(!model.actual_size(), "fit above one must report actual scale truth");
    });
    test("actual-size state describes current valid scales including an exact initial fit", [] {
        GalleryModel model;
        model.set_bounds({0,0,100,100}); model.set_images(images(1,100,100));
        require(model.views().size() == 1 && model.actual_size(), "exact initial fit should report one scale");
        model.toggle_actual_size(); require(model.actual_size(), "all-one branch did not restore initial one scale");
        model.set_bounds({0,0,200,200}); require(!model.actual_size(), "resize above one must clear actual-size fact");
        near(model.views()[0].scale,2,"resize must not freeze a one scale");
    });
    test("A and replacement rebuild current and saved zoom and pan", [] {
        GalleryModel model;
        model.set_bounds({0,0,302,202}); model.set_images(images(2,200,100));
        require(model.views().size() == 2, "A rebuild views missing");
        model.zoom_at(0,4,151,50); model.set_offset(0,80,-12);
        model.toggle_direction(); require(model.horizontal(), "A direction not retained");
        near(model.views()[0].scale,.75,"A did not rebuild horizontal fit");
        near(model.views()[0].saved_scale,.75,"A did not replace saved fit");
        near(model.views()[0].offset_x,0,"A kept user pan"); near(model.views()[0].saved_offset_y,0,"A kept saved pan");
        model.set_images(images(1,200,100));
        require(model.views().size() == 1 && model.horizontal(), "replacement changed direction preference");
        near(model.views()[0].scale,1.51,"replacement did not fit new source");
        near(model.views()[0].saved_scale,1.51,"replacement kept old saved scale");
        model.set_images({}); require(model.views().empty() && !model.actual_size(), "empty replacement did not clear preview");
    });
    test("invalid dimensions remain placeholders and do not determine actual size", [] {
        GalleryModel model;
        model.set_bounds({10,20,202,102}); model.set_images({{"bad-width.png",0,100},{"bad-height.png",100,0},{},{"valid.png",100,50}});
        require(model.views().size() == 4, "invalid dimensions must retain positions");
        for (std::size_t i=0;i<4;++i) {
            finite_rect(model.views()[i].cell); finite_rect(model.image_rect(i));
            require(std::isfinite(model.views()[i].scale) && model.views()[i].scale >= 0,"placeholder scale is invalid");
            if (i<3) require(model.image_rect(i).width == 0 && model.image_rect(i).height == 0,"invalid image must not be drawable");
        }
        require(model.actual_size(), "placeholders must not hide the valid image's one scale");
        model.set_images({{}, {"bad.png",0,100}}); require(!model.actual_size(), "all-invalid views must not report an actual image size");
    });
    test("tiny bounds shrink gaps rather than creating negative cells", [] {
        GalleryModel model;
        model.set_bounds({0,0,1,1}); model.set_images(images(4));
        require(model.views().size() == 4, "tiny views missing");
        for (std::size_t i=0;i<4;++i) {
            const auto cell = model.views()[i].cell; finite_rect(cell); finite_rect(model.image_rect(i));
            require(cell.x >= 0 && cell.y >= 0 && cell.x+cell.width <= 1 && cell.y+cell.height <= 1,"tiny cell escaped bounds");
        }
    });
    test("pan uses truncated center and strict bounds with odd image dimensions", [] {
        GalleryModel model;
        model.set_bounds({0,0,100,100}); model.set_images(images(1,201,101));
        require(model.views().size() == 1,"pan view missing");
        model.set_offset(0,99,99); near(model.views()[0].offset_x,0,"fitted horizontal image must stay centered");
        model.toggle_actual_size(); require(model.actual_size(),"pan test requires one scale");
        model.set_offset(0,99,99); rect(model.image_rect(0),0,0,201,101);
        near(model.views()[0].offset_x,50,"positive horizontal pan bound wrong"); near(model.views()[0].offset_y,0,"positive vertical pan bound wrong");
        model.set_offset(0,-99,-99); rect(model.image_rect(0),-101,-1,201,101);
        near(model.views()[0].offset_x,-51,"negative horizontal pan bound must account for truncation");
        near(model.views()[0].offset_y,-1,"negative vertical pan bound must account for truncation");
    });
    test("zoom preserves the anchor image pixel then clamps to fit and visible bounds", [] {
        GalleryModel model;
        model.set_bounds({10,20,100,100}); model.set_images(images(1,200,200));
        require(model.views().size() == 1,"zoom view missing");
        model.zoom_at(0,2,45,65);
        near(model.views()[0].scale,1,"zoom factor was not applied");
        rect(model.image_rect(0),-25,-25,200,200); // Source pixel (70,90) remains at (45,65).
        near(model.image_rect(0).x+70*model.views()[0].scale,45,"horizontal anchor pixel moved");
        near(model.image_rect(0).y+90*model.views()[0].scale,65,"vertical anchor pixel moved");
        near(model.views()[0].saved_scale,.5,"wheel zoom overwrote initial fit");
        model.zoom_at(0,.01,45,65); near(model.views()[0].scale,.5,"zoom crossed fit lower bound");
        rect(model.image_rect(0),10,20,100,100);
    });
    test("nonfinite bounds offsets and zoom inputs cannot poison geometry", [] {
        GalleryModel model;
        const double nan=std::numeric_limits<double>::quiet_NaN(), inf=std::numeric_limits<double>::infinity();
        model.set_images(images(1,100,100)); model.set_bounds({nan,inf,-1,nan});
        require(model.views().size() == 1,"invalid-bounds view missing"); rect(model.views()[0].cell,0,0,0,0); finite_rect(model.image_rect(0));
        model.set_bounds({0,0,100,100}); model.zoom_at(0,2,50,50); model.set_offset(0,7,-3);
        for (const double factor : {0.0,-1.0,nan,inf}) model.zoom_at(0,factor,50,50);
        model.zoom_at(0,2,nan,50); model.zoom_at(99,2,50,50); model.set_offset(0,nan,4); model.set_offset(0,inf,0); model.set_offset(99,1,1);
        near(model.views()[0].scale,2,"invalid zoom changed scale"); near(model.views()[0].offset_x,7,"invalid offset changed x"); near(model.views()[0].offset_y,-3,"invalid offset changed y"); finite_rect(model.image_rect(0));
    });
    test("extreme finite inputs never overflow returned geometry", [] {
        GalleryModel model;
        const double maximum=(std::numeric_limits<double>::max)();
        model.set_bounds({maximum,maximum,maximum,maximum}); model.set_images(images(4,1,1));
        require(model.views().size() == 4,"extreme views missing");
        model.set_offset(0,maximum,maximum); model.zoom_at(0,maximum,maximum,maximum);
        for (std::size_t i=0;i<4;++i) { finite_rect(model.views()[i].cell); finite_rect(model.image_rect(i)); }
        model.toggle_actual_size(); model.set_offset(0,-maximum,-maximum); finite_rect(model.image_rect(0));
    });
    std::cout << "Gallery model tests: " << checks-failures << '/' << checks << " passed\n";
    return failures ? 1 : 0;
}
