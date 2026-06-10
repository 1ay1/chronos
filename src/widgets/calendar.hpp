#pragma once
// chronos::ui::CalendarWidget — a full-screen, state-of-the-art month view.
//
// Pressing 'c' opens this over a darkened sky. It shows a big vector-font
// month header, a generous day grid with leading/trailing days from the
// neighbouring months dimmed, today on a glowing accent pill, a per-day moon
// phase glyph, weekend tinting, and a side rail with the full date, the day's
// astronomy, ISO week / day-of-year, the season, and upcoming events with
// live countdowns.
//
//   h / l  or  ← / →    previous / next month
//   j / k  or  ↑ / ↓    previous / next week (moves the cursor day)
//   H / L               previous / next year
//   t                   jump back to today
//   c / Esc             close

#include "../widget.hpp"
#include "../font.hpp"
#include "sky.hpp"
#include "../timeutil.hpp"
#include <format>
#include <array>

namespace chronos::ui {

class CalendarWidget : public Widget {
public:
    const char* name() const override { return "calendar"; }

    bool on_key(const maya::Event& ev) override {
        using namespace maya;
        if (key(ev, 'l') || key(ev, SpecialKey::Right)) { step_cursor(+1); return true; }
        if (key(ev, 'h') || key(ev, SpecialKey::Left))  { step_cursor(-1); return true; }
        if (key(ev, 'j') || key(ev, SpecialKey::Down))  { step_cursor(+7); return true; }
        if (key(ev, 'k') || key(ev, SpecialKey::Up))    { step_cursor(-7); return true; }
        if (key(ev, 'n') || key(ev, 'L'))               { step_month(+1); return true; }
        if (key(ev, 'p') || key(ev, 'H'))               { step_month(-1); return true; }
        if (key(ev, '}') || key(ev, ']'))               { step_year(+1);  return true; }
        if (key(ev, '{') || key(ev, '['))               { step_year(-1);  return true; }
        if (key(ev, 't')) { y_ = m_ = cur_ = 0; return true; }
        return false;
    }

    // ── full-screen paint ──────────────────────────────────────────────────
    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const Theme& th = c.theme;
        const int W = r.w, H = r.h;
        float sun_alt = (float)c.sun.altitude;

        // 1) frosted backdrop: dim the live sky to a deep glass, with a subtle
        //    vertical gradient so the screen has depth.
        for (int cy = 0; cy < H; ++cy) {
            float v = float(cy) / std::max(1, H - 1);
            Col top = gfx::mix(sky_bg(sun_alt, cy * 2,     H * 2), th.scrim, 0.86f);
            Col bot = gfx::mix(sky_bg(sun_alt, cy * 2 + 1, H * 2), th.scrim, 0.86f);
            // darken further toward the bottom for a vignette
            float vig = 0.10f + 0.10f * v;
            top = gfx::scale(top, 1.f - vig);
            bot = gfx::scale(bot, 1.f - vig);
            for (int cx = 0; cx < W; ++cx)
                p.cell(r.x + cx, r.y + cy, top, bot);
        }

        // resolve the displayed month
        int year  = y_ ? y_ : c.lt.tm_year + 1900;
        int month = m_ ? m_ : c.lt.tm_mon + 1;
        int today_y = c.lt.tm_year + 1900, today_m = c.lt.tm_mon + 1, today_d = c.lt.tm_mday;
        int cursor = cur_ ? cur_ : (year == today_y && month == today_m ? today_d : 1);

        int dim  = days_in_month(year, month);
        int fdow = first_dow_monday(year, month);     // 0=Mon .. 6=Sun

        // ── layout: grid panel (left ~70%) + side rail (right) ──────────────
        int rail_w = std::clamp(W / 4, 22, 34);
        int gap    = 2;
        Rect grid{r.x + 2, r.y + 1, W - rail_w - gap - 4, H - 2};
        Rect rail{grid.right() + gap, r.y + 1, rail_w, H - 2};

        int header_bottom = paint_header(p, grid, c, MON_FULL(month), year);
        paint_grid(p, grid, c, header_bottom, year, month, dim, fdow, cursor,
                   today_y, today_m, today_d);
        paint_rail(p, rail, c, year, month, cursor, dim);

        // footer hint bar, centred
        std::string hint =
            "h/l day  \u00b7  j/k week  \u00b7  n/p month  \u00b7  [ ] year  \u00b7  t today  \u00b7  c close";
        int hx = r.x + std::max(0, (W - (int)gfx::utf8_cols(hint)) / 2);
        p.text(hx, r.y + H - 1, hint, th.text_dim, scrim_bg(p, sun_alt, H - 1));
    }

private:
    int y_ = 0, m_ = 0;     // displayed year/month (0 = follow today)
    int cur_ = 0;           // cursor day within displayed month (0 = auto)

    // ── header: big month name + year in the vector font. Returns the row
    //    just below the header (where the weekday row should start).
    int paint_header(Painter& p, const Rect& g, const Ctx& c,
                     const std::string& mon, int year) {
        const Theme& th = c.theme;
        float sun_alt = (float)c.sun.altitude;
        auto skybg = [&](int, int cy) { return scrim_bg(p, sun_alt, cy); };

        // header band height is fixed (responsive cells live below it). The big
        // year is sized to that band so it never bleeds into the grid.
        int band = std::clamp(g.h / 5, 4, 8);          // header rows
        float em = band * 4 * 0.92f;                    // octant-rows for the year

        std::string ys = std::format("{}", year);
        float yw = font::measure_em(ys) * em / 2.f;
        float yx = g.right() - yw - 1;
        Col contour{0.02f, 0.03f, 0.07f};
        Col glow    = gfx::scale(th.accent, 0.45f);
        Col yr_top  = gfx::mix(th.accent, Col{1,1,1}, 0.55f);   // bright top
        Col yr_bot  = th.accent;                                 // accent bottom
        font::draw_text(p, yx, (float)g.y, em, ys, contour, skybg, 0.22f);
        font::draw_text_grad(p, yx, (float)g.y, em, ys, yr_top, yr_bot, skybg,
                             0.15f, glow, em * 0.16f);

        // month name as bold caps text, vertically centred in the band. (The
        // vector font only defines digits/punct — letters have no strokes, so
        // the month must be plain text, not draw_text.)
        std::string up;
        for (char ch : mon) up += (char)std::toupper((unsigned char)ch);
        int my = g.y + std::max(0, (band - 1) / 2);
        // a soft accent underline tying it to the year baseline
        int ulen = (int)gfx::utf8_cols(up);
        for (int i = 0; i < ulen + 2; ++i)
            p.text(g.x + i, my + 1, "\u2500", gfx::scale(th.warm, 0.5f), skybg(0, my + 1));
        p.text(g.x, my, up, th.warm, skybg(0, my), true);

        // accent rule under the band
        int ruley = g.y + band;
        for (int cx = g.x; cx < g.right(); ++cx)
            p.text(cx, ruley, "\u2500", th.panel_border, skybg(cx, ruley));
        return ruley + 1;
    }

    // ── the month grid (Android-style: weekday header, then a grid of soft
    //    cards that fills the remaining height). Cells are height-capped so a
    //    tall screen pads BETWEEN weeks instead of stretching cells into a
    //    void; the grid block is vertically centred in the leftover space. ──
    void paint_grid(Painter& p, const Rect& g, const Ctx& c, int header_bottom,
                    int year, int month, int dim, int fdow, int cursor,
                    int ty, int tm, int td) {
        const Theme& th = c.theme;
        float sun_alt = (float)c.sun.altitude;
        auto bg = [&](int cx, int cy) { (void)cx; return scrim_bg(p, sun_alt, cy); };

        int cell_w = g.w / 7;
        int gx = g.x + (g.w - cell_w * 7) / 2;        // centre the 7 columns

        // weekday header row
        static const char* WD[7] = {"MON","TUE","WED","THU","FRI","SAT","SUN"};
        int wd_y = header_bottom;
        for (int i = 0; i < 7; ++i) {
            Col cc = (i >= 5) ? th.bad : th.text_dim;
            int x = gx + i * cell_w + (cell_w - 3) / 2;
            p.text(x, wd_y, WD[i], cc, bg(x, wd_y), true);
        }

        int grid_top = wd_y + 1;
        int rows = (fdow + dim + 6) / 7;              // weeks actually needed
        rows = std::clamp(rows, 4, 6);
        int span = g.bottom() - grid_top;
        if (span < rows) return;                      // too short to draw

        // Fill the WHOLE span: cells take a flat base height, and the leftover
        // rows are spread as a 1-row gutter between weeks (and the remainder
        // padded onto the cells themselves) so the grid always reaches the
        // bottom — no dead-zone, no stretched void.
        int gut       = (span >= rows * 4 + (rows - 1)) ? 1 : 0;
        int body      = span - gut * (rows - 1);      // rows available for cells
        int cell_base = body / rows;
        int extra     = body - cell_base * rows;      // give first `extra` rows +1

        int pm = month - 1, pmy = year; if (pm < 1) { pm = 12; pmy--; }
        int pdim = days_in_month(pmy, pm);

        int cy0 = grid_top;
        for (int row = 0; row < rows; ++row) {
            int cell_h = cell_base + (row < extra ? 1 : 0);
            for (int col = 0; col < 7; ++col) {
                int idx = row * 7 + col;
                int daynum; bool in_month; int cellM, cellYr;
                if (idx < fdow) {
                    daynum = pdim - (fdow - 1 - idx);
                    in_month = false; cellM = pm; cellYr = pmy;
                } else if (idx >= fdow + dim) {
                    daynum = idx - (fdow + dim) + 1;
                    in_month = false;
                    cellM = month + 1; cellYr = year; if (cellM > 12) { cellM = 1; cellYr++; }
                } else {
                    daynum = idx - fdow + 1;
                    in_month = true; cellM = month; cellYr = year;
                }
                int cx = gx + col * cell_w;
                paint_day_cell(p, c, cx, cy0, cell_w - 1, cell_h,
                               daynum, cellYr, cellM, in_month,
                               in_month && daynum == cursor,
                               in_month && cellYr == ty && cellM == tm && daynum == td,
                               col >= 5);
            }
            cy0 += cell_h + gut;
        }
    }

    // one day cell, Android-calendar style: a soft rounded card. The day
    // number sits top-left; the moon dot rides the bottom-right. Today gets a
    // filled accent pill behind the number; the cursor gets a tinted card with
    // a rounded accent outline.
    void paint_day_cell(Painter& p, const Ctx& c, int x, int y, int w, int h,
                        int day, int year, int month, bool in_month,
                        bool is_cursor, bool is_today, bool weekend) {
        const Theme& th = c.theme;
        (void)c;
        // Flat tile colors — a constant glass, NOT the per-row sky scrim, so
        // every cell reads with identical brightness top-to-bottom.
        Col tile_bg{0.055f, 0.065f, 0.105f};          // dark blue-grey glass
        Col card = in_month ? tile_bg : gfx::scale(tile_bg, 0.45f);
        if (is_today)  card = gfx::mix(tile_bg, th.accent, 0.22f);
        if (is_cursor) card = gfx::mix(tile_bg, th.accent, 0.28f);
        for (int cy = y; cy < y + h; ++cy)
            for (int cx = x; cx < x + w; ++cx)
                p.text(cx, cy, " ", card, card);

        // cursor: a rounded accent outline hugging the card (only when the
        // card is tall enough to host a border without crushing the body).
        bool boxed = is_cursor && h >= 4 && w >= 5;
        if (boxed) {
            Col oc = th.accent;
            p.text(x,         y,         "\u256d", oc, card);
            p.text(x + w - 1, y,         "\u256e", oc, card);
            p.text(x,         y + h - 1, "\u2570", oc, card);
            p.text(x + w - 1, y + h - 1, "\u256f", oc, card);
            for (int cx = x + 1; cx < x + w - 1; ++cx) {
                p.text(cx, y,         "\u2500", oc, card);
                p.text(cx, y + h - 1, "\u2500", oc, card);
            }
            for (int cy = y + 1; cy < y + h - 1; ++cy) {
                p.text(x,         cy, "\u2502", oc, card);
                p.text(x + w - 1, cy, "\u2502", oc, card);
            }
        }

        Col num_fg;
        if (!in_month)      num_fg = gfx::scale(th.text_dim, 0.55f);
        else if (weekend)   num_fg = gfx::mix(th.bad, th.text, 0.20f);
        else                num_fg = th.text;
        if (is_cursor)      num_fg = th.text;

        // day number, top-left (inset 1 when the card has a border)
        std::string ds = std::format("{}", day);
        int pad = boxed ? 1 : 0;
        int ndx = x + 1 + pad, ndy = y + pad;
        if (is_today) {
            int chip_w = (int)ds.size() + 2;
            for (int cx = ndx; cx < ndx + chip_w && cx < x + w; ++cx)
                p.text(cx, ndy, " ", th.accent, th.accent);
            p.text(ndx + 1, ndy, ds, th.panel_bg, th.accent, true);
        } else {
            p.text(ndx, ndy, ds, num_fg, card, in_month || is_cursor);
        }

        // moon phase dot, bottom-right corner (so it never collides with the
        // number or today pill, and the card body reads as occupied). Skip it
        // when the card is too short or the cursor border owns that corner.
        int min_w = (int)ds.size() + 3 + (boxed ? 2 : 0);
        bool moon_room = h >= (boxed ? 4 : 2);
        if (in_month && w >= min_w && moon_room) {
            std::time_t noon = timeutil::make_midnight(year, month, day) + 43200;
            auto mp = chronos::astro::moon_phase(noon);
            const char* sym = moon_symbol(mp.frac);
            Col mc = gfx::mix(gfx::scale(th.text_dim, 0.80f), th.cool, (float)mp.illum);
            int mx = x + w - 2 - pad, my = y + h - 1 - pad;
            p.text(mx, my, sym, mc, card);
        }
    }

    // ── side rail: date, astronomy, week info, events ───────────────────────
    void paint_rail(Painter& p, const Rect& r, const Ctx& c,
                    int year, int month, int cursor, int dim) {
        const Theme& th = c.theme;
        Col bg = th.panel_bg;
        p.panel(r.x, r.y, r.w, r.h, bg, th.panel_border);
        Rect in = r.inset(1);
        int x = in.x + 1, y = in.y;

        // selected date headline
        int d = std::clamp(cursor, 1, dim);
        int dow = dow_monday(year, month, d);          // 0=Mon
        static const char* WDF[7] = {"Monday","Tuesday","Wednesday","Thursday",
                                     "Friday","Saturday","Sunday"};
        p.text(x, y, "SELECTED", th.accent, bg, true);
        p.text(x, y + 1, std::format("{}", WDF[dow]), th.text, bg, true);
        p.text(x, y + 2, std::format("{} {} {}", d, MON_FULL(month), year),
               th.text_dim, bg);

        // big day number in the vector font
        float em = std::clamp(in.w * 4 * 0.22f, 18.f, 44.f);
        float sun_alt = (float)c.sun.altitude;
        auto cardbg = [&](int, int){ return bg; };
        std::string ds = std::format("{}", d);
        float dw = font::measure_em(ds) * em / 2.f;
        float dx = in.x + (in.w - dw) / 2.f;
        Col contour{0.02f, 0.03f, 0.07f};
        Col dglow  = gfx::scale(th.accent, 0.45f);
        Col d_top  = gfx::mix(th.accent, Col{1,1,1}, 0.55f);
        Col d_bot  = th.accent;
        font::draw_text(p, dx, (float)(y + 4), em, ds, contour, cardbg, 0.22f);
        font::draw_text_grad(p, dx, (float)(y + 4), em, ds, d_top, d_bot, cardbg,
                             0.15f, dglow, em * 0.16f);
        int yy = y + 4 + (int)(em / 4.f) + 1;

        rule(p, in.x, yy, in.w, th.panel_border, bg); yy++;

        // moon phase for the selected day
        std::time_t noon = timeutil::make_midnight(year, month, d) + 43200;
        auto mp = chronos::astro::moon_phase(noon);
        p.text(x, yy, "MOON", th.cool, bg, true); yy++;
        p.text(x, yy, std::format("{}  {}", moon_symbol(mp.frac), mp.name),
               th.text, bg); yy++;
        int gw = std::max(5, in.w - 7);
        p.gauge(x, yy, gw, (float)mp.illum, th.cool, th.panel_border, bg);
        p.text(x + gw + 1, yy, std::format("{:.0f}%", mp.illum * 100),
               th.text_dim, bg); yy += 2;

        rule(p, in.x, yy, in.w, th.panel_border, bg); yy++;

        // week / day-of-year / season facts
        int doy  = chronos::astro::day_of_year(year, month, d);
        int week = iso_week(year, month, d);
        p.text(x, yy, "AT A GLANCE", th.accent, bg, true); yy++;
        p.text(x, yy, std::format("Day {} of {}", doy, year_len(year)), th.text_dim, bg); yy++;
        p.text(x, yy, std::format("ISO week {:02}", week), th.text_dim, bg); yy++;
        p.text(x, yy, std::format("{}  {}", season_mark(month, d), season(month, d)),
               th.text_dim, bg); yy += 2;

        rule(p, in.x, yy, in.w, th.panel_border, bg); yy++;

        // upcoming events with countdowns
        p.text(x, yy, "UPCOMING", th.accent, bg, true); yy++;
        auto evs = chronos::timeutil::upcoming_events(c.now);
        for (auto& e : evs) {
            if (yy >= in.bottom()) break;
            std::string label = std::format("\u2022 {}", e.name);
            std::string cd = e.days == 0 ? "today" : std::format("{}d", e.days);
            int budget = in.w - (int)gfx::utf8_cols(cd) - 3;
            label = clip_cols(label, budget);
            Col lc = e.days == 0 ? th.good : th.text;
            p.text(x, yy, label, lc, bg);
            int cdx = in.right() - (int)gfx::utf8_cols(cd) - 1;
            Col cc = e.days == 0 ? th.good : th.warn;
            p.text(cdx, yy, cd, cc, bg, true);
            yy++;
        }
        (void)sun_alt;
    }

    // truncate a UTF-8 string to fit `cols` display columns, adding … if cut.
    static std::string clip_cols(const std::string& s, int cols) {
        if ((int)gfx::utf8_cols(s) <= cols) return s;
        if (cols <= 1) return "\u2026";
        std::string out; int w = 0;
        for (size_t i = 0; i < s.size();) {
            char32_t cp; int n = gfx::utf8_decode(s, i, cp);
            int cw = (cp >= 0x1100) ? 2 : 1;   // rough wide check
            if (w + cw > cols - 1) break;
            out.append(s, i, n); w += cw; i += n;
        }
        out += "\u2026";
        return out;
    }

    // ── small drawing helpers ─────────────────────────────────────
    void rule(Painter& p, int x, int y, int w, Col c, Col bg) {
        for (int i = 0; i < w; ++i) p.text(x + i, y, "\u2500", c, bg);
    }
    Col scrim_bg(Painter& p, float sun_alt, int cy) {
        Col s = sky_bg(sun_alt, cy * 2, p.rows() * 2);
        return gfx::mix(s, Col{0.02f,0.02f,0.05f}, 0.84f);
    }

    // ── calendar math ───────────────────────────────────────────────────────
    static bool is_leap(int y) { return (y%4==0 && y%100!=0) || (y%400==0); }
    static int year_len(int y) { return is_leap(y) ? 366 : 365; }
    static int days_in_month(int y, int m) {
        static const int t[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        return (m == 2 && is_leap(y)) ? 29 : t[m];
    }
    // Zeller-ish day of week, 0=Monday .. 6=Sunday.
    static int dow_monday(int y, int m, int d) {
        static const int tt[] = {0,3,2,5,0,3,5,1,4,6,2,4};
        int yy = y; if (m < 3) yy -= 1;
        int w = (yy + yy/4 - yy/100 + yy/400 + tt[m-1] + d) % 7;  // 0=Sun
        return (w + 6) % 7;                                       // → 0=Mon
    }
    static int first_dow_monday(int y, int m) { return dow_monday(y, m, 1); }

    static int iso_week(int y, int m, int d) {
        // ISO 8601 week number.
        int doy = chronos::astro::day_of_year(y, m, d);
        int wday = dow_monday(y, m, d) + 1;            // 1=Mon..7=Sun
        int week = (doy - wday + 10) / 7;
        if (week < 1) return iso_week(y - 1, 12, 31);
        if (week > 52) {
            int wday_dec31 = dow_monday(y, 12, 31) + 1;
            if (wday_dec31 < 4) return 1;
        }
        return week;
    }

    static const char* MON_FULL(int m) {
        static const char* M[] = {"","January","February","March","April","May",
            "June","July","August","September","October","November","December"};
        return M[std::clamp(m,1,12)];
    }
    // compact moon symbol by cycle fraction
    static const char* moon_symbol(double frac) {
        int idx = (int)std::floor(frac * 8.0 + 0.5) % 8;
        static const char* S[8] = {
            "\u25cf",  // new (dark dot)
            "\u25d0",  // waxing crescent
            "\u25d1",  // first quarter
            "\u25d1",  // waxing gibbous
            "\u25cb",  // full (open dot)
            "\u25d1",  // waning gibbous
            "\u25d1",  // last quarter
            "\u25d0",  // waning crescent
        };
        return S[idx];
    }
    static const char* season(int m, int d) {
        int k = (m == 3 && d >= 20) || (m > 3 && m < 6) || (m == 6 && d < 21) ? 0 :
                (m == 6 && d >= 21) || (m > 6 && m < 9) || (m == 9 && d < 22) ? 1 :
                (m == 9 && d >= 22) || (m > 9 && m < 12) || (m == 12 && d < 21) ? 2 : 3;
        static const char* S[] = {"Spring","Summer","Autumn","Winter"};
        return S[k];
    }
    static const char* season_mark(int m, int d) {
        // U+25xx geometric-shapes block is below the 0x2600 emoji range, so
        // utf8_cols counts these as 1 column — no wide/narrow desync.
        const char* s = season(m, d);
        if (s[0]=='S' && s[1]=='p') return "\u25c6";   // ◆ diamond (spring)
        if (s[0]=='S')              return "\u25c9";   // ◉ fisheye (summer sun)
        if (s[0]=='A')              return "\u25d5";   // ◕ (autumn)
        return "\u25c7";                               // ◇ open diamond (winter)
    }
    static const char* season_glyph(int m, int d) {
        const char* s = season(m, d);
        if (s[0]=='S' && s[1]=='p') return "\xF0\x9F\x8C\xB1";   // 🌱
        if (s[0]=='S')              return "\xE2\x98\x80";        // ☀
        if (s[0]=='A')              return "\xF0\x9F\x8D\x82";    // 🍂
        return "\xE2\x9D\x84";                                   // ❄
    }

    // ── navigation ──────────────────────────────────────────────────────────
    void anchor() {
        if (!y_ || !m_) {
            std::time_t n = std::time(nullptr); std::tm lt{}; localtime_r(&n, &lt);
            y_ = lt.tm_year + 1900; m_ = lt.tm_mon + 1;
            if (!cur_) cur_ = lt.tm_mday;
        }
    }
    void step_month(int d) {
        anchor();
        m_ += d;
        while (m_ < 1)  { m_ += 12; y_--; }
        while (m_ > 12) { m_ -= 12; y_++; }
        cur_ = std::clamp(cur_ ? cur_ : 1, 1, days_in_month(y_, m_));
    }
    void step_year(int d) { anchor(); y_ += d; cur_ = std::clamp(cur_ ? cur_ : 1, 1, days_in_month(y_, m_)); }
    void step_cursor(int delta) {
        anchor();
        cur_ = (cur_ ? cur_ : 1) + delta;
        while (cur_ < 1)  { step_month(-1); cur_ += days_in_month(y_, m_); }
        while (cur_ > days_in_month(y_, m_)) { cur_ -= days_in_month(y_, m_); step_month(+1); }
    }
};

} // namespace chronos::ui
