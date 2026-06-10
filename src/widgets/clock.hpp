#pragma once
// chronos::ui::ClockWidget — big scalable-font clock + date, over the sky.
// Press 'a' to cycle the clock size (huge / big / compact line).

#include "../widget.hpp"
#include "../font.hpp"
#include "sky.hpp"
#include <format>

namespace chronos::ui {

class ClockWidget : public Widget {
public:
    const char* name() const override { return "clock"; }
    bool on_key(const maya::Event& ev) override {
        if (maya::key(ev, 'a')) { size_ = (size_ + 1) % 3; return true; }
        return false;
    }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        static const char* WD[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                                   "THURSDAY","FRIDAY","SATURDAY"};
        static const char* MON[] = {"","January","February","March","April","May",
            "June","July","August","September","October","November","December"};
        float sun_alt = (float)c.sun.altitude;
        bool night = sun_alt < -6.f;
        Col ink    = night ? Col{0.92f,0.94f,1.0f} : Col{0.08f,0.10f,0.16f};
        Col accent = night ? Col{0.78f,0.86f,1.0f} : Col{1.0f,0.99f,0.94f};
        int ph = r.h * 2;
        auto bg = [&](int, int cy) { return sky_scrim(sun_alt, cy, ph); };

        int x = r.x, y = r.y;
        if (size_ == 2) {  // compact single line
            p.text_over(x, y, std::format("{:02}:{:02}:{:02}",
                c.lt.tm_hour, c.lt.tm_min, c.lt.tm_sec), accent, bg, true);
            y += 2;
            date_line(p, x, y, c, WD, MON, ink, bg);
            return;
        }

        // scalable crisp vector clock: em height in OCTANT rows (= 4× cell rows).
        float em_q = (size_ == 0) ? (r.h * 4 * 0.72f) : (r.h * 4 * 0.46f);
        em_q = std::clamp(em_q, 20.f, 200.f);

        std::string hhmm = std::format("{:02}:{:02}", c.lt.tm_hour, c.lt.tm_min);
        float bx = (float)r.x;
        float by = (float)r.y;     // cell-space top

        float endx = font::draw_text(p, bx, by, em_q, hhmm, accent, bg, 0.115f);

        // seconds, smaller, vertically centred against the big digits.
        float secs_q = em_q * 0.38f;
        float secs_y = by + (em_q - secs_q) / 2.f / 4.f;   // (octant-rows → cell-rows)
        font::draw_text(p, endx + 0.5f, secs_y, secs_q,
                        std::format("{:02}", c.lt.tm_sec), ink, bg, 0.14f);

        int date_y = int(by + em_q / 4.f) + 1;
        date_line(p, x, date_y, c, WD, MON, ink, bg);
    }

private:
    template <class BgFn>
    void date_line(Painter& p, int x, int y, const Ctx& c,
                   const char* const* WD, const char* const* MON,
                   Col ink, BgFn&& bg) {
        p.text_over(x, y, std::format("{} \u00b7 {} {}, {}",
            WD[c.lt.tm_wday % 7], MON[c.lt.tm_mon + 1], c.lt.tm_mday,
            c.lt.tm_year + 1900), ink, bg, true);
    }
    int size_ = 0;   // 0 huge, 1 big, 2 compact
};

} // namespace chronos::ui
