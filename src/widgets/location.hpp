#pragma once
// chronos::ui::LocationWidget — top-right place pill + a "now / warp" badge.
// Floats over the sky (uses the sky scrim for legibility).

#include "../widget.hpp"
#include "sky.hpp"
#include <format>

namespace chronos::ui {

class LocationWidget : public Widget {
public:
    const char* name() const override { return "location"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        float sun_alt = (float)c.sun.altitude;
        bool night = sun_alt < -6.f;
        Col accent = night ? Col{0.72f,0.82f,1.0f} : Col{1.0f,0.97f,0.9f};
        int ph = r.h * 2;
        auto bg = [&](int, int cy) { return sky_scrim(sun_alt, cy, ph); };

        std::string place = "\u27e1 " + c.place;
        int px = r.right() - (int)gfx::utf8_cols(place) - 1;
        p.text_over(std::max(r.x, px), r.y, place, accent, bg, true);

        // warp / live badge underneath
        std::string badge = c.warp_rate != 0
            ? std::format("\u23e9 warp x{:.0f}", std::abs(c.warp_rate) / 60.0)
            : (c.time_warp != 0 ? std::format("{:+.0f}h", c.time_warp / 3600.0)
                                : std::string("\u25cf live"));
        Col bc = c.warp_rate != 0 ? Col{1.0f,0.7f,0.4f}
               : (c.time_warp != 0 ? Col{0.8f,0.8f,1.0f} : Col{0.6f,0.95f,0.7f});
        int bx = r.right() - (int)gfx::utf8_cols(badge) - 1;
        p.text_over(std::max(r.x, bx), r.y + 1, badge, bc, bg, true);
    }
};

} // namespace chronos::ui
