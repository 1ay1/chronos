#pragma once
// chronos::ui::SunArcWidget — graphical sun path with sunrise/sunset markers.
//
// Draws the sun's daily arc as a parabola across the card; a glowing dot rides
// the arc at the current time. Sunrise/sunset times anchor the ends, a
// daylight-length readout sits below, and a progress gauge shows how far
// through the daylight (or night) we are.

#include "../widget.hpp"
#include "sky.hpp"
#include <cmath>
#include <format>

namespace chronos::ui {

class SunArcWidget : public Widget {
public:
    const char* name() const override { return "sun"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const Theme& th = c.theme;
        p.panel(r.x, r.y, r.w, r.h, th.panel_bg, th.panel_border);
        Rect in = r.inset(1);
        p.text(in.x + 1, in.y, "\u2600 SUN", th.warm, th.panel_bg, true);

        auto& st = c.sun_times;
        auto hm = [](double h) {
            int hh = (int)h % 24, mm = (int)std::round((h - std::floor(h)) * 60);
            if (mm == 60) { mm = 0; hh = (hh + 1) % 24; }
            return std::format("{:02}:{:02}", hh, mm);
        };

        if (!st.valid || st.always_up || st.always_down) {
            const char* msg = st.always_up ? "midnight sun \u2600"
                            : st.always_down ? "polar night \u263e"
                            : "set CHRONOS_LAT/LON";
            p.text(in.x + 1, in.y + 2, msg, th.text_dim, th.panel_bg);
            return;
        }

        // arc band geometry (in sub-pixels) inside the card
        int arc_x0 = in.x, arc_w = in.w;
        int top_y  = (in.y + 1) * 2;
        int base_y = (in.bottom() - 3) * 2;
        float now_h = c.lt.tm_hour + c.lt.tm_min / 60.0f + c.lt.tm_sec / 3600.0f;
        float prog  = (now_h - (float)st.sunrise_h) / std::max(0.01f, (float)st.daylight_h);
        bool daytime = now_h >= st.sunrise_h && now_h <= st.sunset_h;
        float pc = std::clamp(prog, 0.f, 1.f);

        float sun_px = arc_x0 + pc * arc_w;
        float sun_py = base_y - std::sin(pc * 3.14159f) * (base_y - top_y);
        Col sun_col   = daytime ? gfx::hex(0xffd479) : gfx::hex(0x7681b4);
        Col sky_lo = gfx::mix(th.panel_bg, gfx::hex(0x1b2540), 0.5f);   // arc fill tint

        auto shade = [&](float px, float py) -> Col {
            Col col = th.panel_bg;
            float t = (px - arc_x0) / std::max(1.f, (float)arc_w);
            float ay = base_y - std::sin(std::clamp(t,0.f,1.f) * 3.14159f) * (base_y - top_y);

            // soft daylight wash under the arc (sky gradient inside the dome)
            if (t >= 0 && t <= 1 && py > ay && py < base_y) {
                float depth = (py - ay) / std::max(1.f, base_y - ay);
                Col wash = gfx::mix(gfx::scale(sun_col, 0.22f), sky_lo, depth);
                col = gfx::mix(col, wash, 0.5f * (daytime ? 1.f : 0.4f));
            }
            // horizon line
            float hd = std::abs(py - base_y);
            if (hd < 1.0f) col = gfx::mix(col, th.panel_border,
                                          gfx::smoothstep(1.0f, 0.f, hd) * 0.85f);
            // crisp arc curve (thin, AA)
            if (t >= 0 && t <= 1) {
                float dd = std::abs(py - ay);
                Col arc_col = gfx::mix(th.panel_border, sun_col, 0.55f);
                col = gfx::mix(col, arc_col, gfx::smoothstep(1.1f, 0.2f, dd));
            }
            // sun disc: limb-darkened core + radial glow
            float ds = std::hypot(px - sun_px, (py - sun_py));
            float R = 2.6f;
            if (daytime) {
                float glow = std::pow(gfx::smoothstep(R * 4.f, 0.f, ds), 1.5f) * 0.6f;
                col = gfx::add(col, gfx::scale(gfx::hex(0xffcf6b), glow));
                float disc = gfx::smoothstep(R + 0.7f, R - 0.7f, ds);
                float limb = 1.f - 0.3f * gfx::smoothstep(0.f, R, ds);
                col = gfx::mix(col, gfx::scale(gfx::hex(0xfff3cf), limb + 0.15f), disc);
            } else {
                float disc = gfx::smoothstep(R + 0.6f, R - 0.6f, ds);
                col = gfx::add(col, gfx::scale(sun_col,
                               gfx::smoothstep(R * 2.5f, 0.f, ds) * 0.3f));
                col = gfx::mix(col, sun_col, disc);
            }
            return col;
        };

        // supersample horizontally so the arc + disc read crisp, not blurry.
        constexpr int SSx = 4;
        auto srow = [&](float xl, float sub_y) {
            Col a{0,0,0};
            for (int s = 0; s < SSx; ++s) {
                Col c0 = shade(xl + (s + 0.5f) / SSx, sub_y);
                a.r += c0.r; a.g += c0.g; a.b += c0.b;
            }
            return gfx::scale(a, 1.f / SSx);
        };
        for (int cy = in.y + 1; cy < in.bottom() - 2; ++cy)
            for (int cx = in.x; cx < in.right(); ++cx)
                p.cell(cx, cy, srow(float(cx), cy * 2 + 0.5f),
                               srow(float(cx), cy * 2 + 1.5f));

        // endpoint labels just above the horizon line
        int label_y = in.bottom() - 3;
        p.text(in.x, label_y, "\u2191" + hm(st.sunrise_h), th.warm, th.panel_bg);
        std::string set = hm(st.sunset_h) + "\u2193";
        p.text(in.right() - (int)gfx::utf8_cols(set), label_y, set,
               gfx::hex(0xbb9af7), th.panel_bg);

        // daylight readout + progress gauge at the bottom
        int dh = (int)st.daylight_h, dm = (int)std::round((st.daylight_h - dh) * 60);
        p.text(in.x, in.bottom() - 1,
               std::format("{}h{:02}m daylight", dh, dm), th.text_dim, th.panel_bg);
        std::string alts = std::format("{:+.0f}\u00b0", c.sun.altitude);
        p.text(in.right() - (int)alts.size(), in.bottom() - 1, alts,
               daytime ? th.warm : th.text_dim, th.panel_bg);
    }
};

} // namespace chronos::ui
