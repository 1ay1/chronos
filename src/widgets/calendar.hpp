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

        // 1) frosted backdrop: a deep twilight-glass gradient with real depth.
        //    Top is a cool indigo, easing to a slightly warmer, darker base at
        //    the bottom; a faint warm bloom sits low-centre like a horizon glow
        //    bleeding through the glass. This gives the panel atmosphere instead
        //    of a flat dark fill.
        Col deep_top{0.045f, 0.055f, 0.105f};   // cool indigo glass
        Col deep_bot{0.020f, 0.022f, 0.045f};   // darker, faintly warm base
        for (int cy = 0; cy < H; ++cy) {
            float v = float(cy) / std::max(1, H - 1);          // 0 top .. 1 bottom
            float ev = v * v * (3.f - 2.f * v);                // eased gradient
            Col base = gfx::mix(deep_top, deep_bot, ev);
            // a soft warm horizon bloom low and centred for depth
            float hb = gfx::smoothstep(0.55f, 0.95f, v) *
                       (1.f - gfx::smoothstep(0.92f, 1.0f, v));
            base = gfx::add(base, gfx::scale(Col{0.10f, 0.06f, 0.03f}, hb * 0.5f));
            // subtle vertical sub-pixel split so the gradient is smooth
            Col top = base;
            Col bot = gfx::mix(base, gfx::mix(deep_top, deep_bot,
                               std::min(1.f, ev + 0.5f / H)), 1.f);
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
            Col cc = (i >= 5) ? gfx::mix(th.cool, th.text_dim, 0.35f) : th.text_dim;
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
        // Glassy tiles: a soft vertical gradient (lighter top → darker bottom)
        // reads as frosted glass with a sheen, not a flat fill. In-month cells
        // are a cool blue-grey; out-of-month days recede into the backdrop.
        Col tile_top{0.075f, 0.090f, 0.140f};
        Col tile_bot{0.040f, 0.050f, 0.090f};
        if (!in_month) { tile_top = gfx::scale(tile_top, 0.42f); tile_bot = gfx::scale(tile_bot, 0.42f); }
        // weekends get a faint cool wash so they read distinct without shouting
        if (weekend && in_month) {
            tile_top = gfx::mix(tile_top, th.cool, 0.06f);
            tile_bot = gfx::mix(tile_bot, th.cool, 0.04f);
        }
        // today: warm amber-tinted glass; cursor: accent-blue glass. Today uses
        // WARM so it never visually merges with the (cool) cursor highlight.
        if (is_today)  { tile_top = gfx::mix(tile_top, th.warm, 0.16f);
                         tile_bot = gfx::mix(tile_bot, th.warm, 0.10f); }
        if (is_cursor) { tile_top = gfx::mix(tile_top, th.accent, 0.26f);
                         tile_bot = gfx::mix(tile_bot, th.accent, 0.18f); }
        Col card = tile_top;   // representative card colour for text backdrops
        for (int cy = y; cy < y + h; ++cy) {
            // per-row interpolation down the tile for the glass gradient
            float tt = (h > 1) ? float(cy - y) / float(h - 1) : 0.f;
            Col rowc = gfx::mix(tile_top, tile_bot, tt * tt * (3.f - 2.f * tt));
            for (int cx = x; cx < x + w; ++cx)
                p.text(cx, cy, " ", rowc, rowc);
        }

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
        else if (weekend)   num_fg = gfx::mix(th.cool, th.text, 0.30f);   // cool, not alarming red
        else                num_fg = th.text;
        if (is_cursor)      num_fg = Col{1,1,1};

        // day number, top-left (inset 1 when the card has a border)
        std::string ds = std::format("{}", day);
        int pad = boxed ? 1 : 0;
        int ndx = x + 1 + pad, ndy = y + pad;
        if (is_today) {
            // warm-gold pill behind dark text — the one hot accent on the grid
            int chip_w = (int)ds.size() + 2;
            for (int cx = ndx; cx < ndx + chip_w && cx < x + w; ++cx)
                p.text(cx, ndy, " ", th.warm, th.warm);
            p.text(ndx + 1, ndy, ds, gfx::scale(th.panel_bg, 0.5f), th.warm, true);
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
            Col mc = gfx::mix(gfx::scale(th.text_dim, 1.05f), th.cool, (float)mp.illum);
            int mx = x + w - 2 - pad, my = y + h - 1 - pad;
            p.text(mx, my, sym, mc, card);
        }
    }

    // ── side rail: date, astronomy, week info, events ───────────────────────
    void paint_rail(Painter& p, const Rect& r, const Ctx& c,
                    int year, int month, int cursor, int dim) {
        const Theme& th = c.theme;
        // A slightly lifted glass so the rail reads as a distinct panel sitting
        // ON the backdrop, not blended into it.
        Col bg = gfx::mix(th.panel_bg, Col{0.06f, 0.07f, 0.12f}, 0.6f);
        p.panel(r.x, r.y, r.w, r.h, bg, th.panel_border);
        Rect in = r.inset(1);
        int x = in.x + 1, y = in.y;
        int rx = in.right() - 1;       // right text edge for values

        // selected date headline
        int d = std::clamp(cursor, 1, dim);
        int dow = dow_monday(year, month, d);          // 0=Mon
        static const char* WDF[7] = {"Monday","Tuesday","Wednesday","Thursday",
                                     "Friday","Saturday","Sunday"};
        section(p, x, in.right(), in.y, "SELECTED", th.accent, bg);
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

        // The rail content is laid out in stacked sections. Rather than top-
        // align everything (which strands a huge empty glass void at the foot,
        // as in the old layout), we measure the natural content height and add
        // a uniform breathing gap between sections so the body fills the rail.
        int doy   = chronos::astro::day_of_year(year, month, d);
        int week  = iso_week(year, month, d);
        std::time_t noon = timeutil::make_midnight(year, month, d) + 43200;
        auto mp = chronos::astro::moon_phase(noon);
        double tzoff = c.tz_offset;
        auto st = chronos::astro::sun_times(year, month, d, c.lat, c.lon, tzoff);
        auto evs = chronos::timeutil::upcoming_events(c.now);
        int n_ev = std::min((int)evs.size(), 4);

        // section row costs: MOON 3, SUN 4, GLANCE 4, YEAR 3, UPCOMING 1+n_ev
        int content = 3 + 4 + 4 + 3 + (1 + n_ev);
        int avail   = in.bottom() - yy;                // rows left below the day
        int slack   = std::max(0, avail - content);
        int gap     = std::clamp(slack / 5, 0, 3);     // spread across 5 gaps

        // ── MOON ────────────────────────────────────────────────────────────
        section(p, x, in.right(), yy, "MOON", th.cool, bg); yy++;
        p.text(x, yy, std::format("{}  {}", moon_symbol(mp.frac), mp.name),
               th.text, bg); yy++;
        int gw = std::max(5, in.w - 7);
        p.gauge(x, yy, gw, (float)mp.illum, th.cool, th.panel_border, bg);
        p.text(x + gw + 1, yy, std::format("{:.0f}%", mp.illum * 100),
               th.text_dim, bg); yy += 1 + gap;

        // ── SUN: rise / set / daylight for the selected day ──────────────────
        section(p, x, in.right(), yy, "SUN", th.warm, bg); yy++;
        if (st.valid) {
            kv(p, x, rx, yy, "\u2191 Rise", clock_hm(st.sunrise_h), th.text_dim, th.text, bg); yy++;
            kv(p, x, rx, yy, "\u2193 Set",  clock_hm(st.sunset_h),  th.text_dim, th.text, bg); yy++;
            kv(p, x, rx, yy, "Daylight", std::format("{:.1f}h", st.daylight_h),
               th.text_dim, th.warm, bg); yy += 1 + gap;
        } else {
            p.text(x, yy, st.always_up ? "Polar day" : "Polar night",
                   th.text_dim, bg); yy += 3 + gap;
        }

        // ── AT A GLANCE: week / day-of-year / season ─────────────────────────
        section(p, x, in.right(), yy, "AT A GLANCE", th.accent, bg); yy++;
        kv(p, x, rx, yy, "Day of year", std::format("{}/{}", doy, year_len(year)),
           th.text_dim, th.text, bg); yy++;
        kv(p, x, rx, yy, "ISO week", std::format("{:02}", week), th.text_dim, th.text, bg); yy++;
        kv(p, x, rx, yy, season(month, d),
           std::string(season_mark(month, d)), th.text_dim, th.cool, bg); yy += 1 + gap;

        // ── YEAR PROGRESS: a thin bar of how far through the year we are ──────
        section(p, x, in.right(), yy, "YEAR", th.cool, bg); yy++;
        float yprog = float(doy) / float(year_len(year));
        int ygw = std::max(5, in.w - 7);
        p.gauge(x, yy, ygw, yprog, th.warm, th.panel_border, bg);
        p.text(x + ygw + 1, yy, std::format("{:.0f}%", yprog * 100),
               th.text_dim, bg); yy += 1 + gap;

        // ── UPCOMING events with countdowns ──────────────────────────────────
        section(p, x, in.right(), yy, "UPCOMING", th.accent, bg); yy++;
        for (int i = 0; i < n_ev; ++i) {
            auto& e = evs[i];
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

    // section header: a filled accent CHIP holding a letter-spaced label, with
    // a gradient hairline trailing to the panel edge that fades the accent into
    // the glass. The chip + tracking make the titles pop as real section banners
    // instead of a bare bold word on a faint line.
    void section(Painter& p, int x, int right, int y, const char* label,
                 Col accent, Col bg) {
        // letter-space the label for a premium, banner-like feel: "MOON" -> "M O O N"
        std::string spaced;
        for (const char* s = label; *s; ++s) {
            if (s != label) spaced += ' ';
            spaced += *s;
        }
        int lw = (int)gfx::utf8_cols(spaced);

        // filled accent chip behind the label: dark ink on the bright accent,
        // one cell of padding each side (mirrors the today-pill on the grid).
        Col chip   = accent;
        Col chip_d = gfx::scale(accent, 0.55f);          // shaded chip underside
        Col ink    = gfx::scale(bg, 0.5f);               // near-black label ink
        int cx0 = x, cx1 = x + lw + 2;                   // chip spans [cx0, cx1)
        for (int cx = cx0; cx < cx1; ++cx)
            p.cell(cx, y, chip, chip_d);                 // 2 sub-px = subtle sheen
        p.text(x + 1, y, spaced, ink, chip, true);

        // bright leading edge bar + a soft glow cell right of the chip
        p.cell(cx1, y, gfx::scale(accent, 0.75f), gfx::scale(accent, 0.35f));

        // gradient hairline from after the chip to the panel edge: starts at the
        // accent and eases down into the background so it reads as a fade, not a
        // flat dim line.
        int hx0 = cx1 + 1, hx1 = right - 1;
        int span = std::max(1, hx1 - hx0);
        for (int cx = hx0; cx < hx1; ++cx) {
            float t = float(cx - hx0) / float(span);     // 0 near chip .. 1 far
            Col line = gfx::mix(gfx::scale(accent, 0.55f), bg, t * t);
            p.text(cx, y, "\u2500", line, bg);
        }
    }

    // a label : value row, value right-aligned to `rx`.
    void kv(Painter& p, int x, int rx, int y, const std::string& key,
            const std::string& val, Col kc, Col vc, Col bg) {
        p.text(x, y, key, kc, bg);
        int vx = rx - (int)gfx::utf8_cols(val) + 1;
        p.text(vx, y, val, vc, bg, true);
    }

    // local solar hours (0-24) → "HH:MM".
    static std::string clock_hm(double h) {
        if (h < 0) h += 24; if (h >= 24) h -= 24;
        int hh = (int)h; int mm = (int)std::lround((h - hh) * 60.0);
        if (mm == 60) { mm = 0; hh = (hh + 1) % 24; }
        return std::format("{:02}:{:02}", hh, mm);
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
    // backdrop colour behind grid chrome (weekday row, rules, header). Matches
    // the deep twilight-glass background so text sits flush on it.
    Col scrim_bg(Painter& p, float sun_alt, int cy) {
        (void)sun_alt;
        float v = float(cy) / std::max(1, p.rows() - 1);
        float ev = v * v * (3.f - 2.f * v);
        return gfx::mix(Col{0.045f,0.055f,0.105f}, Col{0.020f,0.022f,0.045f}, ev);
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
