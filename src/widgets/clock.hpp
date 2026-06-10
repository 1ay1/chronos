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
        auto bg     = [&](int, int cy) { return sky_scrim(sun_alt, cy, ph); };
        auto skybg  = [&](int, int cy) { return sky_bg(sun_alt, cy, ph); };

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

        // Premium look: (1) a soft wide GLOW that bleeds the accent into the
        // sky around the glyph (luminous, like the digits emit light); (2) a
        // tight dark outline for legibility on bright skies; (3) the body as a
        // vertical GRADIENT — bright cool-white at the top fading to the accent
        // at the bottom, the backlit-glass sheen high-end clock UIs have.
        Col contour{0.02f, 0.03f, 0.07f};
        Col glow    = gfx::scale(accent, 0.40f);
        Col top_ink = night ? Col{1.0f, 1.0f, 1.0f} : Col{0.20f, 0.24f, 0.34f};
        Col bot_ink = accent;
        // glow: a fat pass drawn FIRST; the outline + body overpaint the core,
        // leaving a dim accent ring around the strokes = a neon-tube halo.
        font::draw_text(p, bx, by, em_q, hhmm, glow, skybg, 0.30f);
        font::draw_text(p, bx, by, em_q, hhmm, contour, skybg, 0.20f);
        font::draw_text_grad(p, bx, by, em_q, hhmm, top_ink, bot_ink, skybg, 0.135f);
        // draw_text advances in sub-x units (2 per cell); convert to cells /2.
        float endx = bx + font::measure_em(hhmm) * em_q / 2.f;

        // seconds, smaller, sitting AFTER the minutes and baseline-aligned to
        // the bottom of the big digits (superscript style).
        float secs_q = em_q * 0.40f;
        float secs_y = by + (em_q - secs_q) / 4.f;   // (octant-rows → cell-rows)
        std::string ss = std::format("{:02}", c.lt.tm_sec);
        font::draw_text(p, endx + 1.0f, secs_y, secs_q, ss, contour, skybg, 0.22f);
        font::draw_text_grad(p, endx + 1.0f, secs_y, secs_q, ss, top_ink, bot_ink, skybg, 0.135f);

        int date_y = int(by + em_q / 4.f) + 1;
        date_line(p, x, date_y, c, WD, MON, ink, bg);
    }

private:
    template <class BgFn>
    void date_line(Painter& p, int x, int y, const Ctx& c,
                   const char* const* WD, const char* const* MON,
                   Col ink, BgFn&& bg) {
        // A light, sky-tinted scrim (not a hard 40% black band) keeps the date
        // readable without looking like a solid box under the digits.
        float sun_alt = (float)c.sun.altitude;
        auto soft = [&](int cx, int cy) {
            (void)cx;
            Col s = sky_bg(sun_alt, cy, p.rows() * 2);
            return gfx::mix(s, Col{0,0,0}, 0.12f);
        };
        std::string d = std::format("{}  \u00b7  {} {}, {}",
            WD[c.lt.tm_wday % 7], MON[c.lt.tm_mon + 1], c.lt.tm_mday,
            c.lt.tm_year + 1900);
        p.text_over(x + 1, y, d, ink, soft, true);
        (void)bg;
    }
    int size_ = 0;   // 0 huge, 1 big, 2 compact
};

} // namespace chronos::ui
