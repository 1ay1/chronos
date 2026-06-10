#pragma once
// chronos::ui::ClockWidget — big seven-segment clock + date, over the sky.
// Press 'a' to toggle between the big glyph clock and a compact line.

#include "../widget.hpp"
#include "sky.hpp"
#include <format>

namespace chronos::ui {

namespace bigfont {
inline const char* G[11][5] = {
    {" \u259f\u2588\u2599 ","\u2588\u2598 \u259d\u2588","\u2588   \u2588","\u2588\u2596 \u2597\u2588"," \u259c\u2588\u259b "}, // 0
    {"  \u2588  "," \u2588\u2588  ","  \u2588  ","  \u2588  "," \u2588\u2588\u2588 "}, // 1
    {" \u259f\u2588\u2599 ","\u2598  \u259d\u2588","  \u259f\u2588\u2598"," \u259f\u2598  ","\u259f\u2588\u2588\u2588\u2596"}, // 2
    {"\u259f\u2588\u2588\u2599 ","   \u259f\u2588"," \u259c\u2588\u2599 ","   \u259f\u2588","\u259c\u2588\u2588\u259b "}, // 3
    {"\u2588  \u2588 ","\u2588  \u2588 ","\u259c\u2588\u2588\u2588\u2596","   \u2588 ","   \u2588 "}, // 4
    {"\u2588\u2588\u2588\u2588\u2596","\u2588    ","\u259c\u2588\u2588\u2599 ","   \u259f\u2588","\u259c\u2588\u2588\u259b "}, // 5
    {" \u259f\u2588\u2599 ","\u2588\u2598   ","\u2588\u2588\u2599\u2596 ","\u2588\u2598 \u259d\u2588"," \u259c\u2588\u259b "}, // 6
    {"\u2588\u2588\u2588\u2588\u258c","   \u259f\u2598","  \u259f\u2598 "," \u259f\u2598  "," \u2588   "}, // 7
    {" \u259f\u2588\u2599 ","\u2588\u2598 \u259d\u2588"," \u259c\u2588\u259b ","\u2588\u2596 \u2597\u2588"," \u259c\u2588\u259b "}, // 8
    {" \u259f\u2588\u2599 ","\u2588\u2598 \u259d\u2588"," \u259c\u2588\u2588\u258c","   \u259f\u2588"," \u259c\u2588\u259b "}, // 9
    {"     ","  \u2597  ","     ","  \u2597  ","     "}, // :
};
inline int idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ':') return 10;
    return -1;
}
} // namespace bigfont

class ClockWidget : public Widget {
public:
    const char* name() const override { return "clock"; }
    bool on_key(const maya::Event& ev) override {
        if (maya::key(ev, 'a')) { big_ = !big_; return true; }
        return false;
    }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        static const char* WD[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                                   "THURSDAY","FRIDAY","SATURDAY"};
        static const char* MON[] = {"","January","February","March","April","May",
            "June","July","August","September","October","November","December"};
        float sun_alt = (float)c.sun.altitude;
        bool night = sun_alt < -6.f;
        Col txt    = night ? Col{0.85f,0.88f,1.0f} : Col{0.06f,0.08f,0.14f};
        Col accent = night ? Col{0.72f,0.82f,1.0f} : Col{1.0f,0.97f,0.9f};
        int ph = r.h * 2;
        auto bg = [&](int, int cy) { return sky_scrim(sun_alt, cy, ph); };

        int x = r.x, y = r.y;
        if (big_) {
            std::string hhmm = std::format("{:02}:{:02}", c.lt.tm_hour, c.lt.tm_min);
            int gx = x;
            for (char ch : hhmm) {
                int gi = bigfont::idx(ch);
                if (gi < 0) { gx += 2; continue; }
                for (int row = 0; row < 5; ++row)
                    p.text_over(gx, y + row, bigfont::G[gi][row], accent, bg, true);
                gx += (ch == ':') ? 3 : 6;
            }
            p.text_over(gx, y + 4, std::format(":{:02}", c.lt.tm_sec), txt, bg);
            y += 6;
        } else {
            p.text_over(x, y, std::format("{:02}:{:02}:{:02}",
                c.lt.tm_hour, c.lt.tm_min, c.lt.tm_sec), accent, bg, true);
            y += 2;
        }
        p.text_over(x, y, std::format("{} \u00b7 {} {}, {}",
            WD[c.lt.tm_wday % 7], MON[c.lt.tm_mon + 1], c.lt.tm_mday, c.lt.tm_year + 1900),
            txt, bg, true);
    }

private:
    bool big_ = true;
};

} // namespace chronos::ui
