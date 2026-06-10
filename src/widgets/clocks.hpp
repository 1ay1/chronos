#pragma once
// chronos::ui::WorldClocksWidget — multi-zone clock card.
// Each row: label, a graphical day/night dot, the time, and the date.

#include "../widget.hpp"
#include "../timeutil.hpp"
#include <format>

namespace chronos::ui {

class WorldClocksWidget : public Widget {
public:
    const char* name() const override { return "clocks"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const Theme& th = c.theme;
        p.panel(r.x, r.y, r.w, r.h, th.panel_bg, th.panel_border);
        Rect in = r.inset(1);
        p.text(in.x + 1, in.y, "\u25f4 WORLD CLOCKS", th.cool, th.panel_bg, true);

        auto zones = chronos::timeutil::default_zones();
        int row = in.y + 2;

        // a small "sun across the day" dot for an hour-of-day 0..24
        auto daydot = [&](float hour) -> std::pair<const char*, Col> {
            // night → ☾ dim, dawn/dusk → warm, midday → bright sun
            if (hour < 6 || hour >= 19) return {"\u263e", th.text_dim};
            if (hour < 8 || hour >= 17) return {"\u2600", th.warm};
            return {"\u2600", gfx::hex(0xffd479)};
        };

        auto draw_row = [&](const std::string& label, int hh, int mm,
                            const char* wd, int day, const char* mon,
                            bool is_local) {
            if (row >= in.bottom()) return;
            Col lc = is_local ? th.good : th.cool;
            auto [glyph, gcol] = daydot(hh + mm / 60.f);
            int x = in.x + 1;
            p.text(x, row, std::format("{:<6}", label), lc, th.panel_bg, true);
            x += 6;
            p.text(x, row, glyph, gcol, th.panel_bg); x += 2;
            p.text(x, row, std::format("{:02}:{:02}", hh, mm), th.text, th.panel_bg); x += 6;
            p.text(x, row, std::format("{} {} {}", wd, day, mon), th.text_dim, th.panel_bg);
            row++;
        };

        static const char* WD3[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* MON3[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                     "Jul","Aug","Sep","Oct","Nov","Dec"};
        draw_row("Local", c.lt.tm_hour, c.lt.tm_min, WD3[c.lt.tm_wday % 7],
                 c.lt.tm_mday, MON3[c.lt.tm_mon % 12], true);
        for (auto& z : zones) {
            auto rd = chronos::timeutil::read_zone(c.now, z);
            draw_row(rd.label, rd.hour, rd.minute, rd.weekday.c_str(),
                     rd.day, rd.month_abbr.c_str(), false);
        }
        p.text(in.x + 1, in.bottom() - 1, "w close", th.text_dim, th.panel_bg);
    }
};

} // namespace chronos::ui
