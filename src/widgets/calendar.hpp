#pragma once
// chronos::ui::CalendarWidget — a frosted month-grid card.
// Today is highlighted on an accent pill; weekends tinted; h/l navigate months.

#include "../widget.hpp"
#include <format>

namespace chronos::ui {

class CalendarWidget : public Widget {
public:
    const char* name() const override { return "calendar"; }

    bool on_key(const maya::Event& ev) override {
        if (maya::key(ev, 'l') || maya::key(ev, maya::SpecialKey::Right)) { step(+1); return true; }
        if (maya::key(ev, 'h') || maya::key(ev, maya::SpecialKey::Left))  { step(-1); return true; }
        if (maya::key(ev, 't')) { y_ = m_ = 0; return true; }   // back to today
        return false;
    }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        static const char* MON[] = {"","January","February","March","April","May",
            "June","July","August","September","October","November","December"};
        const Theme& th = c.theme;

        int year  = y_ ? y_ : c.lt.tm_year + 1900;
        int month = m_ ? m_ : c.lt.tm_mon + 1;
        bool leap = (year%4==0 && year%100!=0) || (year%400==0);
        static const int dimt[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        int dim = (month==2 && leap) ? 29 : dimt[month];
        static const int tt[] = {0,3,2,5,0,3,5,1,4,6,2,4};
        int yy = year; if (month < 3) yy -= 1;
        int fdow = ((yy + yy/4 - yy/100 + yy/400 + tt[month-1] + 1) % 7 + 6) % 7;

        p.panel(r.x, r.y, r.w, r.h, th.panel_bg, th.panel_border);
        Rect in = r.inset(1);
        std::string title = std::format("{} {}", MON[month], year);
        p.text(in.x + std::max(0, (in.w - (int)title.size()) / 2), in.y,
               title, th.warn, th.panel_bg, true);

        const char* wd = "Mo Tu We Th Fr Sa Su";
        int gx = in.x + std::max(0, (in.w - 20) / 2);
        // header — weekend cols tinted
        for (int i = 0; i < 7; ++i) {
            Col cc = (i >= 5) ? th.bad : th.text_dim;
            p.text(gx + i * 3, in.y + 1, std::string(wd + i * 3, 2), cc, th.panel_bg);
        }

        int day = 1, row = 2;
        while (day <= dim && in.y + row < in.bottom()) {
            for (int col = 0; col < 7; ++col) {
                if ((day == 1 && col < fdow) || day > dim) continue;
                bool today = (year == c.lt.tm_year + 1900 &&
                              month == c.lt.tm_mon + 1 && day == c.lt.tm_mday);
                bool wknd = col >= 5;
                Col fg = today ? th.panel_bg : (wknd ? th.bad : th.text);
                Col bg = today ? th.accent : th.panel_bg;
                std::string s = std::format("{:2}", day);
                int cx = gx + col * 3;
                p.text(cx, in.y + row, s, fg, bg, today);
                day++;
            }
            row++;
        }
        p.text(in.x, in.bottom() - 1, "h/l \u2190\u2192 \u00b7 t today \u00b7 c close",
               th.text_dim, th.panel_bg);
    }

private:
    int y_ = 0, m_ = 0;   // 0 = follow today
    void step(int d) {
        // lazily anchor on the current real month the first time we move
        if (!y_ || !m_) { std::time_t n = std::time(nullptr); std::tm lt{}; localtime_r(&n,&lt);
                          y_ = lt.tm_year + 1900; m_ = lt.tm_mon + 1; }
        m_ += d;
        while (m_ < 1)  { m_ += 12; y_--; }
        while (m_ > 12) { m_ -= 12; y_++; }
    }
};

} // namespace chronos::ui
