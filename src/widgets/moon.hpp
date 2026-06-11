#pragma once
// chronos::ui::MoonWidget — a graphical phase-accurate moon plus stats.
//
// Draws the moon as a real disc on the half-block canvas: the lit fraction
// is computed from the phase angle and the terminator is rendered as a
// shaded ellipse, so a waxing crescent actually looks like a crescent. An
// illumination gauge and the phase name sit beside it inside a frosted card.

#include "../widget.hpp"
#include "sky.hpp"
#include <cmath>
#include <format>

namespace chronos::ui {

class MoonWidget : public Widget {
public:
    const char* name() const override { return "moon"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const Theme& th = c.theme;
        p.panel(r.x, r.y, r.w, r.h, th.panel_bg, th.panel_border);
        Rect in = r.inset(1);
        p.text(in.x + 1, in.y, "\u263e MOON", th.cool, th.panel_bg, true);

        // ── draw the moon disc (left side) ──────────────────────────────────
        // The disc lives in a square-ish region on the left of the card. Its
        // radius is bounded by BOTH the available height (in sub-pixels) and the
        // left-column width (in cells, ×2 for sub-pixel aspect), so it stays a
        // true circle no matter the card's proportions instead of a squished
        // blob. Centre it in that region.
        int   inner_rows = in.h;                       // cell rows inside the card
        float avail_h    = inner_rows * 2.f;           // sub-pixels of height
        // reserve the right ~55% of the card for stats on a wide card; on a
        // narrow card the disc may use most of the width and stats stack below.
        bool  wide       = in.w >= 24;
        float disc_cols  = wide ? std::min((float)in.w * 0.42f, 14.f)
                                : (float)in.w - 2.f;
        // radius: fit inside the height (leave a 1-subpix margin top/bottom) and
        // inside disc_cols (×2 because 1 cell = 2 sub-pixels wide in our aspect).
        // R is in SUB-PIXEL units; x is scaled ×2 below so the disc is a true
        // circle despite cells being twice as tall as wide.
        float R = std::min(avail_h * 0.5f - 1.f, disc_cols * 2.f);
        R = std::max(R, 5.f);
        // centre of the disc, in sub-pixel space (x also in sub-pixels = cell*2)
        float cxp = (in.x + 1.f) * 2.f + R;            // 1-cell left margin
        float cyp = in.y * 2.f + avail_h * 0.5f;       // vertically centred
        float frac = (float)c.moon.frac;              // 0 new .. .5 full .. 1 new
        // illuminated fraction & which limb is lit
        float illum = (float)c.moon.illum;            // 0..1
        bool waxing = frac < 0.5f;                     // lit on the right when waxing

        Col lit  = gfx::hex(0xdfe6ff);
        Col dark = gfx::hex(0x1a1d2e);
        Col halo = gfx::hex(0x2a2f4a);
        // earthshine: the dark side is faintly lit by light bouncing off Earth,
        // strongest near new moon (a thin lit crescent + a ghostly full disc).
        Col earth = gfx::hex(0x232842);
        float earthshine = gfx::smoothstep(0.45f, 0.02f, illum);  // 0 full .. 1 new

        auto moon_at = [&](float px, float py) -> Col {
            // px,py are in SUB-PIXEL space here (caller passes cell*2 for x).
            float dx = (px - cxp), dy = (py - cyp);
            float d = std::sqrt(dx * dx + dy * dy);
            if (d > R + 1.5f) return th.panel_bg;
            if (d > R) return gfx::mix(th.panel_bg, halo, gfx::smoothstep(R + 1.5f, R, d));
            // inside disc: terminator. x' normalised -1..1 across the disc.
            float xn = dx / (R + 1e-3f);
            // The terminator is an ellipse whose width = cos(phase angle).
            // illum in [0,1]; terminator position k in [-1,1].
            float k = (illum * 2.f - 1.f);            // -1 new .. +1 full
            bool inside_lit;
            if (waxing) inside_lit = (xn > -k);       // lit grows from right
            else        inside_lit = (xn <  k);       // lit shrinks from right
            // edge smoothing along the terminator (tight, AA against the fine
            // supersample grid so the crescent edge reads as a clean curve)
            float edge = waxing ? (xn + k) : (k - xn);
            float t = gfx::smoothstep(-0.05f, 0.05f, edge);
            // dark side carries a faint earthshine glow near new moon
            Col darkside = gfx::mix(dark, earth, earthshine);
            Col blended = gfx::mix(darkside, lit, t);
            // surface detail: large smooth maria (dark seas) + small bright
            // craters, both stronger on the lit side. Two noise octaves give a
            // believable lunar face without looking like generic static.
            float litness = inside_lit ? 1.f : (t * 0.5f + earthshine * 0.4f);
            if (litness > 0.05f) {
                // maria: broad dark patches (use normalised disc coords so the
                // pattern is stable regardless of disc size)
                float un = dx / R, vn = dy / R;
                float mare = gfx::fbm(un * 3.4f + 3.f, vn * 3.4f + 1.f);
                blended = gfx::mix(blended, gfx::scale(blended, 0.78f),
                                   gfx::smoothstep(0.50f, 0.82f, mare) * 0.55f * litness);
                // craters: small bright/dark speckles with a rim highlight
                float cr = gfx::fbm(un * 9.0f + 9.f, vn * 9.0f + 5.f);
                float crater = gfx::smoothstep(0.72f, 0.90f, cr);
                blended = gfx::add(blended, gfx::scale(Col{0.10f,0.11f,0.16f},
                                   crater * 0.6f * litness));
            }
            // limb darkening: a soft falloff toward the edge gives the disc a
            // spherical, photographic roundness instead of a flat coin.
            blended = gfx::scale(blended, 1.f - gfx::smoothstep(R * 0.55f, R, d) * 0.30f);
            // bright rim catch on the sunlit limb for a touch of specular pop
            if (inside_lit) {
                float rim = gfx::smoothstep(R * 0.86f, R, d);
                blended = gfx::add(blended, gfx::scale(Col{0.10f,0.12f,0.20f}, rim * 0.5f));
            }
            return blended;
        };

        // High-resolution render: supersample both axes (matching the sun arc)
        // so the terminator curve, limb, and craters read crisp and round
        // rather than stair-stepped. The disc is small, so a dense box filter is
        // cheap. 36 shader samples per emitted half-cell.
        constexpr int SSx = 6, SSy = 6;
        auto msample = [&](float xl, float sub_y_center) -> Col {
            Col a{0,0,0};
            for (int sy = 0; sy < SSy; ++sy) {
                float sub_y = sub_y_center + ((sy + 0.5f) / SSy - 0.5f);
                for (int s = 0; s < SSx; ++s) {
                    // xl is the cell centre in sub-pixel units; a cell spans 2
                    // sub-pixels, so spread the samples across ±1.
                    float sub_x = xl + ((s + 0.5f) / SSx - 0.5f) * 2.f;
                    Col c0 = moon_at(sub_x, sub_y);
                    a.r += c0.r; a.g += c0.g; a.b += c0.b;
                }
            }
            return gfx::scale(a, 1.f / (SSx * SSy));
        };
        for (int cy = in.y; cy < in.bottom(); ++cy)
            for (int cx = in.x; cx < (int)((cxp + R) / 2.f + 2); ++cx) {
                if (cx >= in.right()) break;
                // x passed in sub-pixel units (cell*2); the two emitted half-
                // cells sample the upper and lower sub-pixel rows.
                p.cell(cx, cy, msample(cx * 2.f + 1.f, cy * 2 + 0.5f),
                               msample(cx * 2.f + 1.f, cy * 2 + 1.5f));
            }

        // ── stats (right side) ──────────────────────────────────────────────
        int disc_right = (int)((cxp + R) / 2.f + 2);   // cell just past the disc
        int tx = disc_right + 1;
        if (tx >= in.right() - 6) tx = in.x + 1;     // narrow card → stack below
        int ty = in.y + 1;
        int colw = in.right() - tx;                  // available text width
        auto clip = [&](std::string s) {
            while ((int)gfx::utf8_cols(s) > colw && !s.empty()) {
                // drop trailing UTF-8 byte(s) of the last codepoint
                size_t i = s.size() - 1;
                while (i > 0 && (s[i] & 0xC0) == 0x80) --i;
                s.erase(i);
            }
            return s;
        };
        p.text(tx, ty,     clip(c.moon.glyph + " " + c.moon.name), th.text, th.panel_bg, true);
        p.text(tx, ty + 1, clip(std::format("{:.0f}% lit", illum * 100)),
               th.text_dim, th.panel_bg);
        // illumination gauge
        p.gauge(tx, ty + 2, std::max(4, std::min(colw, 14)),
                illum, th.cool, th.panel_border, th.panel_bg);
        // age in days
        p.text(tx, ty + 3, clip(std::format("{:.1f}d · {}",
               c.moon.age_days, waxing ? "wax" : "wane")),
               th.text_dim, th.panel_bg);
    }
};

} // namespace chronos::ui
