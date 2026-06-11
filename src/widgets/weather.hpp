#pragma once
// chronos::ui::WeatherWidget — a card showing REAL current conditions, fed by
// the Open-Meteo public API (see src/weather.hpp). Big condition glyph + temp
// on the left, a small stats column on the right (feels-like, hi/lo, humidity,
// wind). Falls back to a quiet "fetching…" / "offline" state before the first
// successful fetch or when the network is down.

#include "../widget.hpp"
#include "../weather.hpp"
#include "card.hpp"
#include <cmath>
#include <format>

namespace chronos::ui {

class WeatherWidget : public Widget {
public:
    const char* name() const override { return "weather"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const Theme& th = c.theme;
        const auto& w = c.weather;
        p.panel(r.x, r.y, r.w, r.h, th.panel_bg, th.panel_border);
        Rect in = r.inset(1);

        if (!w.valid) {
            card::title(p, in, "WEATHER", th.cool);
            const char* msg = w.stale ? "offline \u2014 no data yet"
                                      : "fetching live data\u2026";
            card::txt(p, in.x + 1, in.y + 2, msg, th.text_dim);
            card::txt(p, in.x + 1, in.y + 3, "via open-meteo.com", th.text_dim);
            return;
        }

        const char* glyph = chronos::weather::code_glyph(w.code, w.is_day);
        const char* label = chronos::weather::code_label(w.code);
        Col tcol = temp_color(th, w.temp_c);

        // title: accent chip banner + status badge (offline beats day/night)
        if (w.stale)
            card::title(p, in, "WEATHER", th.cool, "offline", th.warn);
        else if (w.is_day)
            card::title(p, in, "WEATHER", th.cool, "\u2600 day", th.warm);
        else
            card::title(p, in, "WEATHER", th.cool, "\u263e night",
                        gfx::mix(th.cool, th.text_dim, 0.3f));

        // Split the card into a left column (big condition + temperature) and a
        // right column (stat rows). The divider sits at a fixed fraction so the
        // two blocks never overprint, with a 1-col gutter each side. On a very
        // narrow card we drop the stat column and keep just the headline.
        int lx    = in.x + 1;
        int split = in.x + std::max(13, in.w * 9 / 20);   // left column width
        bool two_col = (in.right() - 1) - (split + 1) >= 12;
        int gy = in.y + 2;

        // glyph + big temperature on the headline row — floats on the glass
        std::string temp = std::format("{:.0f}\u00b0", w.temp_c);
        card::txt(p, lx, gy, glyph, tcol, true);
        card::txt(p, lx + 2, gy, temp, tcol, true);
        // condition label + feels-like stacked beneath, clipped to the column
        int lwid = (two_col ? split : in.right() - 1) - lx;
        card::txt(p, lx, gy + 1, card::clip_cols(label, lwid), th.text, true);
        // feels-like with an up/down arrow vs. actual temperature
        double fd = w.feels_c - w.temp_c;
        const char* fa = fd > 0.7 ? "\u2191" : fd < -0.7 ? "\u2193" : "\u2248";
        card::txt(p, lx, gy + 2,
                  card::clip_cols(std::format("feels {} {:.0f}\u00b0", fa, w.feels_c), lwid),
                  th.text_dim);

        // ── right column: stat rows, label left / value right-aligned ───────
        if (two_col) {
            int sx = split + 2;
            int rx = in.right() - 2;
            // a faint vertical divider between the columns, fading downward
            for (int cy = gy; cy <= gy + 2; ++cy) {
                float t = float(cy - gy) / 2.f;
                Col bgc = card::frostbg(p, split, cy);
                p.text(split, cy, "\u2502",
                       gfx::mix(gfx::scale(th.panel_border, 1.1f), bgc, t * 0.5f), bgc);
            }
            card::kv(p, sx, rx, gy + 0, "\u2191\u2193 hi/lo",
                     std::format("{:.0f}\u00b0/{:.0f}\u00b0", w.hi_c, w.lo_c),
                     th.text_dim, th.warm);
            card::kv(p, sx, rx, gy + 1, "\u25cb humid",
                     std::format("{}%", w.humidity), th.text_dim, th.cool);
            card::kv(p, sx, rx, gy + 2, "\u2248 wind",
                     std::format("{:.0f} {}", w.wind_kmh,
                                 chronos::weather::wind_compass(w.wind_dir)),
                     th.text_dim, th.text);
            // if the card is tall enough, add a humidity gauge below the stats
            if (gy + 3 < in.bottom() - 1 && rx - sx >= 6)
                card::gauge(p, sx, gy + 3, rx - sx + 1, w.humidity / 100.f, th.cool);
        } else {
            // too narrow for a side column: stack stats full-width below the
            // headline, but only as many as fit above the footer so nothing
            // collides. headline occupies gy..gy+2; footer is bottom-1.
            int rx = in.right() - 2;
            int row = gy + 3;
            int last = in.bottom() - 2;            // keep clear of the footer
            auto stat = [&](const char* k, const std::string& v, Col vc) {
                if (row <= last) { card::kv(p, lx, rx, row, k, v, th.text_dim, vc); row++; }
            };
            stat("\u2191\u2193 hi/lo", std::format("{:.0f}\u00b0/{:.0f}\u00b0", w.hi_c, w.lo_c), th.warm);
            stat("\u25cb humidity", std::format("{}%", w.humidity), th.cool);
            stat("\u2248 wind", std::format("{:.0f} {}", w.wind_kmh,
                          chronos::weather::wind_compass(w.wind_dir)), th.text);
        }

        // footer: data source + age, quiet, riding the glass
        std::string src = "open-meteo \u00b7 " + age_str(c.now, w.fetched);
        card::txt(p, lx, in.bottom() - 1, card::clip_cols(src, in.w - 2),
                  gfx::mix(th.text_dim, th.panel_border, 0.25f));
    }

private:
    // map temperature to a colour: cold→cool blue, mild→text, hot→warm/bad
    static Col temp_color(const Theme& th, double t) {
        if (t <= 0)  return th.cool;
        if (t < 10)  return gfx::mix(th.cool, th.text, (float)(t / 10.0));
        if (t < 22)  return th.text;
        if (t < 30)  return gfx::mix(th.text, th.warm, (float)((t - 22) / 8.0));
        return gfx::mix(th.warm, th.bad, std::min(1.f, (float)((t - 30) / 8.0)));
    }

    static std::string age_str(std::time_t now, std::time_t fetched) {
        long s = (long)(now - fetched);
        if (s < 0) s = 0;
        if (s < 90)    return std::format("{}s ago", s);
        if (s < 5400)  return std::format("{}m ago", (s + 30) / 60);
        return std::format("{}h ago", (s + 1800) / 3600);
    }
};

} // namespace chronos::ui
