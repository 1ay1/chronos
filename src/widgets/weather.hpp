#pragma once
// chronos::ui::WeatherWidget — a card showing REAL current conditions, fed by
// the Open-Meteo public API (see src/weather.hpp). Big condition glyph + temp
// on the left, a small stats column on the right (feels-like, hi/lo, humidity,
// wind). Falls back to a quiet "fetching…" / "offline" state before the first
// successful fetch or when the network is down.

#include "../widget.hpp"
#include "../weather.hpp"
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

        // title, with a faint "stale/offline" marker when data is old
        p.text(in.x + 1, in.y, "\u2601 WEATHER", th.cool, th.panel_bg, true);
        if (w.valid && w.stale) {
            const char* tag = "offline";
            p.text(in.right() - (int)gfx::utf8_cols(tag), in.y, tag, th.warn, th.panel_bg);
        }

        if (!w.valid) {
            const char* msg = w.stale ? "offline \u2014 no data yet"
                                      : "fetching live data\u2026";
            p.text(in.x + 1, in.y + 2, msg, th.text_dim, th.panel_bg);
            p.text(in.x + 1, in.y + 3, "via open-meteo.com", th.text_dim, th.panel_bg);
            return;
        }

        const char* glyph = chronos::weather::code_glyph(w.code, w.is_day);
        const char* label = chronos::weather::code_label(w.code);
        Col tcol = temp_color(th, w.temp_c);

        // Split the card into a left column (big condition + temperature) and a
        // right column (stat rows). The divider sits at a fixed fraction so the
        // two blocks never overprint, with a 1-col gutter each side. On a very
        // narrow card we drop the stat column and keep just the headline.
        int lx    = in.x + 1;
        int split = in.x + std::max(13, in.w * 9 / 20);   // left column width
        bool two_col = (in.right() - 1) - (split + 1) >= 12;
        int gy = in.y + 2;

        // glyph + big temperature on the headline row
        std::string temp = std::format("{:.0f}\u00b0", w.temp_c);
        p.text(lx, gy, glyph, tcol, th.panel_bg, true);
        p.text(lx + 2, gy, temp, tcol, th.panel_bg, true);
        // condition label + feels-like stacked beneath, clipped to the column
        int lwid = (two_col ? split : in.right() - 1) - lx;
        p.text(lx, gy + 1, clip(label, lwid), th.text, th.panel_bg);
        p.text(lx, gy + 2, clip(std::format("feels {:.0f}\u00b0", w.feels_c), lwid),
               th.text_dim, th.panel_bg);

        // ── right column: stat rows, label left / value right-aligned ───────
        if (two_col) {
            int sx = split + 1;
            int rx = in.right() - 1;
            // a faint vertical divider between the columns
            for (int cy = gy; cy <= gy + 2; ++cy)
                p.text(split, cy, "\u2502", gfx::scale(th.panel_border, 0.8f), th.panel_bg);
            kv(p, th, sx, rx, gy + 0, "Hi/Lo",
               std::format("{:.0f}\u00b0/{:.0f}\u00b0", w.hi_c, w.lo_c), th.warm);
            kv(p, th, sx, rx, gy + 1, "Humid",
               std::format("{}%", w.humidity), th.cool);
            kv(p, th, sx, rx, gy + 2, "Wind",
               std::format("{:.0f} {}", w.wind_kmh,
                           chronos::weather::wind_compass(w.wind_dir)), th.text);
        } else {
            // too narrow for a side column: stack stats full-width below the
            // headline, but only as many as fit above the footer so nothing
            // collides. headline occupies gy..gy+2; footer is bottom-1.
            int rx = in.right() - 1;
            int row = gy + 3;
            int last = in.bottom() - 2;            // keep clear of the footer
            auto stat = [&](const char* k, const std::string& v, Col vc) {
                if (row <= last) { kv(p, th, lx, rx, row, k, v, vc); row++; }
            };
            stat("Hi/Lo", std::format("{:.0f}\u00b0/{:.0f}\u00b0", w.hi_c, w.lo_c), th.warm);
            stat("Humidity", std::format("{}%", w.humidity), th.cool);
            stat("Wind", std::format("{:.0f} {}", w.wind_kmh,
                          chronos::weather::wind_compass(w.wind_dir)), th.text);
        }

        // footer: data source + age
        std::string src = "open-meteo \u00b7 " + age_str(c.now, w.fetched);
        p.text(lx, in.bottom() - 1, clip(src, in.w - 2), th.text_dim, th.panel_bg);
    }

private:
    // truncate to fit `cols` display columns, adding an ellipsis if cut.
    static std::string clip(const std::string& s, int cols) {
        if (cols <= 0) return "";
        if ((int)gfx::utf8_cols(s) <= cols) return s;
        if (cols == 1) return "\u2026";
        std::string out; int w = 0;
        for (size_t i = 0; i < s.size();) {
            char32_t cp; int n = gfx::utf8_decode(s, i, cp);
            int cw = (cp >= 0x1100) ? 2 : 1;
            if (w + cw > cols - 1) break;
            out.append(s, i, n); w += cw; i += n;
        }
        out += "\u2026";
        return out;
    }

    // a label : value row, value right-aligned to rx
    static void kv(Painter& p, const Theme& th, int x, int rx, int y,
                   const std::string& key, const std::string& val, Col vc) {
        p.text(x, y, key, th.text_dim, th.panel_bg);
        int vx = rx - (int)gfx::utf8_cols(val) + 1;
        p.text(vx, y, val, vc, th.panel_bg, true);
    }

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
