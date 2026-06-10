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
        // radius in sub-pixels; disc occupies roughly the card height.
        int disc_rows = r.h - 4;
        float R = std::min(disc_rows * 2 * 0.45f, (r.w - 2) * 0.9f);
        float cxp = in.x + R / 2.f + 1.f;             // centre x in cells
        float cyp = (in.y + 1) * 2 + R;               // centre y in sub-pixels
        float frac = (float)c.moon.frac;              // 0 new .. .5 full .. 1 new
        // illuminated fraction & which limb is lit
        float illum = (float)c.moon.illum;            // 0..1
        bool waxing = frac < 0.5f;                     // lit on the right when waxing

        Col lit  = gfx::hex(0xdfe6ff);
        Col dark = gfx::hex(0x1a1d2e);
        Col halo = gfx::hex(0x2a2f4a);

        auto moon_at = [&](float px, float py) -> Col {
            float dx = (px - cxp), dy = (py - cyp) * 0.5f;  // *0.5 → sub-pixel aspect
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
            // edge smoothing along the terminator
            float edge = waxing ? (xn + k) : (k - xn);
            float t = gfx::smoothstep(-0.08f, 0.08f, edge);
            Col base = inside_lit ? lit : dark;
            Col blended = gfx::mix(dark, lit, t);
            // subtle mare texture on the lit side
            if (inside_lit || t > 0.3f) {
                float n = gfx::fbm(px * 0.6f + 3.f, py * 0.3f);
                blended = gfx::mix(blended, gfx::scale(blended, 0.82f),
                                   gfx::smoothstep(0.55f, 0.8f, n) * 0.5f);
            }
            // limb darkening
            blended = gfx::scale(blended, 1.f - gfx::smoothstep(R * 0.6f, R, d) * 0.25f);
            (void)base;
            return blended;
        };

        for (int cy = in.y + 1; cy < in.bottom() - 1; ++cy)
            for (int cx = in.x; cx < (int)(cxp + R / 2.f + 2); ++cx) {
                if (cx >= in.right()) break;
                p.cell(cx, cy, moon_at(cx + 0.5f, cy * 2 + 0.f),
                               moon_at(cx + 0.5f, cy * 2 + 1.f));
            }

        // ── stats (right side) ──────────────────────────────────────────────
        int tx = (int)(cxp + R / 2.f + 3);
        if (tx >= in.right() - 6) tx = in.x + 1;     // narrow card → stack below
        int ty = in.y + 2;
        p.text(tx, ty,     c.moon.glyph + " " + c.moon.name, th.text, th.panel_bg, true);
        p.text(tx, ty + 1, std::format("{:.0f}% illuminated", illum * 100),
               th.text_dim, th.panel_bg);
        // illumination gauge
        p.gauge(tx, ty + 2, std::min(in.right() - tx - 1, 14),
                illum, th.cool, th.panel_border, th.panel_bg);
        // age in days
        p.text(tx, ty + 3, std::format("age {:.1f}d \u00b7 {}",
               c.moon.age_days, waxing ? "waxing" : "waning"),
               th.text_dim, th.panel_bg);
    }
};

} // namespace chronos::ui
