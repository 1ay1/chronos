#pragma once
// chronos::ui::SunArcWidget — graphical sun path with sunrise/sunset markers.
//
// Draws the sun's daily arc as a parabola across the card; a glowing dot rides
// the arc at the current time. A graduated horizon with E/S/W compass ticks
// anchors the scene, sunrise/sunset times sit at the ends, and a daylight
// progress bar with a phase-of-day badge rounds out the card.

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

        float now_h = c.lt.tm_hour + c.lt.tm_min / 60.0f + c.lt.tm_sec / 3600.0f;
        float prog  = (now_h - (float)st.sunrise_h) / std::max(0.01f, (float)st.daylight_h);
        bool daytime = now_h >= st.sunrise_h && now_h <= st.sunset_h;
        float pc = std::clamp(prog, 0.f, 1.f);

        // ── phase-of-day badge in the title bar ─────────────────────────────
        // a small label that names where we are in the day, coloured to match.
        auto [badge, badge_col] = day_phase(c, st, now_h, daytime, pc, th);
        int bw = (int)gfx::utf8_cols(badge);
        p.text(in.right() - bw, in.y, badge, badge_col, th.panel_bg, true);

        // arc band geometry (in sub-pixels) inside the card
        int arc_x0 = in.x, arc_w = in.w;
        int top_y  = (in.y + 1) * 2;
        int base_y = (in.bottom() - 3) * 2;

        float sun_px = arc_x0 + pc * arc_w;
        float sun_py = base_y - std::sin(pc * 3.14159f) * (base_y - top_y);
        Col sun_col   = daytime ? gfx::hex(0xffd479) : gfx::hex(0x7681b4);
        Col sky_lo = gfx::mix(th.panel_bg, gfx::hex(0x1b2540), 0.5f);   // arc fill tint

        auto shade = [&](float px, float py, Col base) -> Col {
            Col col = base;
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
                                          gfx::smoothstep(0.65f, 0.f, hd) * 0.9f);
            // crisp arc curve (thin, AA against the fine supersample grid)
            if (t >= 0 && t <= 1) {
                float dd = std::abs(py - ay);
                Col arc_col = gfx::mix(th.panel_border, sun_col, 0.55f);
                col = gfx::mix(col, arc_col, gfx::smoothstep(0.7f, 0.08f, dd));
                // travelled trail: the portion of today's arc the sun has
                // already crossed glows warm, so the curve reads as a progress
                // track — bright just behind the disc, fading back to sunrise.
                if (daytime && t <= pc) {
                    float behind = (pc - t) / std::max(0.02f, pc);   // 0 at sun .. 1 at rise
                    float trail = gfx::smoothstep(1.1f, 0.f, dd) * (1.f - behind * 0.75f);
                    col = gfx::add(col, gfx::scale(gfx::hex(0xffb454), trail * 0.45f));
                }
                // faint dotted track ahead of the sun (the path still to come)
                if (daytime && t > pc) {
                    float ahead = gfx::smoothstep(0.9f, 0.f, dd);
                    float dash  = 0.5f + 0.5f * std::sin(t * 64.f);   // dotted
                    col = gfx::add(col, gfx::scale(sun_col, ahead * dash * 0.10f));
                }
            }
            // sun disc: limb-darkened core + radial glow, crisp edge
            float ds = std::hypot(px - sun_px, (py - sun_py));
            float R = 2.6f;
            if (daytime) {
                float glow = std::pow(gfx::smoothstep(R * 4.f, 0.f, ds), 1.5f) * 0.6f;
                col = gfx::add(col, gfx::scale(gfx::hex(0xffcf6b), glow));
                float disc = gfx::smoothstep(R + 0.22f, R - 0.22f, ds);
                float limb = 1.f - 0.3f * gfx::smoothstep(0.f, R, ds);
                col = gfx::mix(col, gfx::scale(gfx::hex(0xfff3cf), limb + 0.15f), disc);
            } else {
                float disc = gfx::smoothstep(R + 0.22f, R - 0.22f, ds);
                col = gfx::add(col, gfx::scale(sun_col,
                               gfx::smoothstep(R * 2.5f, 0.f, ds) * 0.3f));
                col = gfx::mix(col, sun_col, disc);
            }
            return col;
        };

        // High-resolution render: supersample BOTH axes heavily so the arc
        // curve and the sun disc read perfectly smooth, not stair-stepped. The
        // panel is only a handful of cells, so a dense 8×6 box filter (48
        // shader samples per emitted half-cell) is cheap yet gives near-vector
        // quality — sub-pixel disc/arc edges resolve as clean gradients.
        constexpr int SSx = 8, SSy = 6;
        auto srow = [&](float xl, float sub_y_center, Col base) {
            Col a{0,0,0};
            for (int sy = 0; sy < SSy; ++sy) {
                // spread sub-rows across the ±0.5 half-cell band around center
                float sub_y = sub_y_center + ((sy + 0.5f) / SSy - 0.5f);
                for (int s = 0; s < SSx; ++s) {
                    Col c0 = shade(xl + (s + 0.5f) / SSx, sub_y, base);
                    a.r += c0.r; a.g += c0.g; a.b += c0.b;
                }
            }
            return gfx::scale(a, 1.f / (SSx * SSy));
        };
        for (int cy = in.y + 1; cy < in.bottom() - 2; ++cy)
            for (int cx = in.x; cx < in.right(); ++cx) {
                // composite over the frosted-glass fill already in this cell so
                // the arc & disc glow through the glass instead of flat black.
                Col base = p.bg_at(cx, cy, th.panel_bg);
                p.cell(cx, cy, srow(float(cx), cy * 2 + 0.5f, base),
                               srow(float(cx), cy * 2 + 1.5f, base));
            }

        // ── compass ticks on the horizon: E (rise side) · S (noon) · W (set) ─
        int horizon_cy = in.bottom() - 3;
        struct Tick { float t; const char* lab; };
        Tick ticks[] = {{0.f, "E"}, {0.5f, "S"}, {1.f, "W"}};
        for (auto& tk : ticks) {
            int tcx = arc_x0 + (int)std::round(tk.t * (arc_w - 1));
            tcx = std::clamp(tcx, in.x, in.right() - 1);
            Col tc = gfx::mix(th.text_dim, th.panel_border, 0.2f);
            p.text(tcx, horizon_cy, tk.lab, tc, p.bg_at(tcx, horizon_cy, th.panel_bg));
        }

        // ── endpoint labels just above the horizon line ─────────────────────
        int label_y = in.bottom() - 3;
        p.text(in.x, label_y, "\u2191" + hm(st.sunrise_h), th.warm, th.panel_bg);
        std::string set = hm(st.sunset_h) + "\u2193";
        p.text(in.right() - (int)gfx::utf8_cols(set), label_y, set,
               gfx::hex(0xbb9af7), th.panel_bg);

        // ── footer: daylight progress bar + readouts ────────────────────────
        int dh = (int)st.daylight_h, dm = (int)std::round((st.daylight_h - dh) * 60);
        int foot = in.bottom() - 1;

        // left: daylight length; right: live altitude
        std::string len = std::format("{}h{:02}m", dh, dm);
        std::string alts = std::format("{:+.0f}\u00b0", c.sun.altitude);
        p.text(in.x, foot, len, th.text, th.panel_bg, true);
        p.text(in.right() - (int)alts.size(), foot,
               alts, daytime ? th.warm : th.text_dim, th.panel_bg, true);

        // centre: a slim daylight progress bar showing how far through the day.
        // it fills warm during daylight and stays dim at night.
        int bar_x = in.x + (int)gfx::utf8_cols(len) + 1;
        int bar_r = in.right() - (int)alts.size() - 1;
        int bar_w = bar_r - bar_x;
        if (bar_w >= 4) {
            Col fillc = daytime ? gfx::hex(0xffb454) : th.text_dim;
            float fillv = daytime ? pc : 0.f;
            p.gauge(bar_x, foot, bar_w, fillv, fillc,
                    gfx::scale(th.panel_border, 0.7f), th.panel_bg);
        }
    }

private:
    // Name the current part of the day and give it a matching tint, so the
    // title bar reads "DAWN" / "NOON" / "DUSK" / "NIGHT" at a glance.
    static std::pair<std::string, Col> day_phase(
            const Ctx& c, const chronos::astro::SunTimes& st,
            float now_h, bool daytime, float pc, const Theme& th) {
        double alt = c.sun.altitude;
        if (!daytime) {
            // before sunrise or after sunset
            if (alt > -6)  return {"twilight", gfx::hex(0x9d7cd8)};
            return {"night", gfx::hex(0x7681b4)};
        }
        // within an hour of sunrise / sunset → golden hour
        if (now_h - st.sunrise_h < 1.0f) return {"dawn", gfx::hex(0xff9e64)};
        if (st.sunset_h - now_h < 1.0f)  return {"dusk", gfx::hex(0xf7768e)};
        if (pc > 0.42f && pc < 0.58f)    return {"noon", gfx::hex(0xffd479)};
        if (pc < 0.5f)                   return {"morning", gfx::hex(0xffcf6b)};
        return {"afternoon", gfx::hex(0xffb454)};
    }
};

} // namespace chronos::ui
