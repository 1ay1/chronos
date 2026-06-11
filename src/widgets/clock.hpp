#pragma once
// chronos::ui::ClockWidget — big scalable-font clock + date, over the sky.
// Press 'a' to cycle the clock size (huge / big / compact line).

#include "../widget.hpp"
#include "../font.hpp"
#include "sky.hpp"
#include <format>
#include <vector>
#include <climits>

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
        bool day   = !night;
        int ph = r.h * 2;
        auto bg     = [&](int, int cy) { return sky_scrim(sun_alt, cy, ph); };

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

        Col contour = day ? Col{0.0f, 0.0f, 0.0f} : Col{0.02f, 0.03f, 0.07f};
        Col glow    = night ? gfx::scale(accent, 0.7f) : Col{0.0f, 0.0f, 0.0f};
        Col top_ink = night ? Col{1.0f, 1.0f, 1.0f}    : Col{0.04f, 0.05f, 0.10f};
        Col bot_ink = night ? accent                   : Col{0.10f, 0.12f, 0.20f};
        float glow_px = night ? em_q * 0.18f : 0.f;    // halo only helps at night

        std::string ss = std::format("{:02}", c.lt.tm_sec);
        float endx   = bx + font::measure_em(hhmm) * em_q / 2.f;
        float secs_q = em_q * 0.40f;
        float secs_y = by + (em_q - secs_q) / 4.f;

        // The vector-font rasterizer (SDF coverage per sub-pixel) is the single
        // most expensive thing in the app (~15ms at 200 cols), and it runs every
        // frame. But the digits only change once a SECOND. So rasterize into a
        // captured cell list and re-blit that each frame; re-rasterize only when
        // the displayed time / size / palette / position actually changes.
        //
        // Two SEPARATE caches: the big HH:MM block changes only once a MINUTE,
        // the small seconds block once a second. Splitting them means the
        // per-second frame only re-rasters the two tiny seconds glyphs (cheap)
        // instead of the whole expensive HH:MM block — keeps every frame well
        // under the 60fps budget (no 1/sec stutter).
        long geom = (long)size_ * 31 + (night ? 1 : 0);
        geom = geom * 131 + (long)std::lround(em_q) * 4099 + r.x * 17 + r.y;
        long sig_hm = ((long)c.lt.tm_hour * 60 + c.lt.tm_min);
        long key_hm = geom * 100003 + sig_hm;
        if (key_hm != hm_key_ || hm_cells_.empty()) {
            hm_key_ = key_hm;
            hm_cells_.clear();
            CaptureSink cap{p.cols(), p.rows(), &hm_cells_};
            auto skybg = [&](int, int cy) { return sky_bg(sun_alt, cy, ph); };
            font::draw_text(cap, bx, by, em_q, hhmm, contour, skybg, 0.20f);
            font::draw_text_grad(cap, bx, by, em_q, hhmm, top_ink, bot_ink, skybg,
                                 0.135f, glow, glow_px);
        }
        long key_ss = geom * 100003 + c.lt.tm_sec + 1000;
        if (key_ss != ss_key_ || ss_cells_.empty()) {
            ss_key_ = key_ss;
            ss_cells_.clear();
            CaptureSink cap{p.cols(), p.rows(), &ss_cells_};
            auto skybg = [&](int, int cy) { return sky_bg(sun_alt, cy, ph); };
            // seconds get their own tinted ink — warm ember by day, cool ice by
            // night — so they read as a deliberate accent next to the big HH:MM
            // instead of a smaller clone of it.
            Col ss_top = night ? Col{0.85f, 0.92f, 1.0f} : Col{0.16f, 0.09f, 0.04f};
            Col ss_bot = night ? Col{0.55f, 0.70f, 1.0f} : Col{0.42f, 0.20f, 0.07f};
            font::draw_text(cap, endx + 2.6f, secs_y, secs_q, ss, contour, skybg, 0.22f);
            font::draw_text_grad(cap, endx + 2.6f, secs_y, secs_q, ss, ss_top, ss_bot,
                                 skybg, 0.135f);
        }
        // Replay the cached glyph cells, re-compositing each onto the LIVE sky.
        // The raster baked a flat palette backdrop (sky_bg); the real sky has
        // clouds/glow drifting behind the digits, so using the baked bg paints
        // stale mismatched boxes around the glyphs. At replay we substitute the
        // backdrop with the sky widget's actual posterized cell colour — cheap
        // (one array lookup per cell) and always matches what's behind.
        auto replay = [&](const std::vector<CapCell>& cells) {
            for (const CapCell& cc : cells) {
                Col top, bot;
                if (sky_live_cell(cc.cx, cc.cy, top, bot)) {
                    Col lb = gfx::mix(top, bot, 0.5f);
                    if (cc.fg.r == cc.bg.r && cc.fg.g == cc.bg.g && cc.fg.b == cc.bg.b) {
                        // glow/tint cell: re-apply the baked glow delta on live sky
                        Col bg0 = sky_bg(sun_alt, cc.cy, ph);
                        Col lit{ gfx::clampf(lb.r + cc.fg.r - bg0.r, 0.f, 1.f),
                                 gfx::clampf(lb.g + cc.fg.g - bg0.g, 0.f, 1.f),
                                 gfx::clampf(lb.b + cc.fg.b - bg0.b, 0.f, 1.f) };
                        p.glyph_cell(cc.cx, cc.cy, cc.glyph, lit, lit);
                    } else {
                        p.glyph_cell(cc.cx, cc.cy, cc.glyph, cc.fg, lb);
                    }
                } else {
                    p.glyph_cell(cc.cx, cc.cy, cc.glyph, cc.fg, cc.bg);
                }
            }
        };
        replay(hm_cells_);
        replay(ss_cells_);

        int date_y = int(by + em_q / 4.f) + 1;
        date_line(p, x, date_y, c, WD, MON, ink, bg);
    }

private:
    // a captured glyph cell + a sink that records draw_text* output instead of
    // painting it, so the rasterization can be reused across frames.
    struct CapCell { int cx, cy; char32_t glyph; Col fg, bg; };
    struct CaptureSink {
        int cols_, rows_;
        std::vector<CapCell>* out;
        int cols() const { return cols_; }
        int rows() const { return rows_; }
        void glyph_cell(int cx, int cy, char32_t g, Col fg, Col bg) {
            out->push_back({cx, cy, g, fg, bg});
        }
    };
    template <class BgFn>
    void date_line(Painter& p, int x, int y, const Ctx& c,
                   const char* const* WD, const char* const* MON,
                   Col ink, BgFn&& bg) {
        // Weekday rides a solid accent CHIP (warm by day, cool accent by night,
        // dark ink — same treatment as the calendar section titles), and the
        // date follows on a soft live-sky scrim. Reads as designed type, not a
        // highlighted text box.
        float sun_alt = (float)c.sun.altitude;
        bool night = sun_alt < -6.f;
        const Theme& th = c.theme;
        auto soft = [&](int cx, int cy) {
            Col top, bot;
            Col s = sky_live_cell(cx, cy, top, bot)
                  ? gfx::mix(top, bot, 0.5f)
                  : sky_bg(sun_alt, cy, p.rows() * 2);
            return gfx::mix(s, Col{0,0,0}, 0.12f);
        };

        // ── weekday chip ──
        std::string wd = WD[c.lt.tm_wday % 7];
        Col chip = night ? th.accent : th.warm;
        Col cink{0.04f, 0.04f, 0.06f};
        int cw = (int)wd.size() + 2;
        for (int i = 0; i < cw; ++i)
            p.text(x + 1 + i, y, " ", cink, chip);
        p.text(x + 2, y, wd, cink, chip, true);

        // ── date text after the chip ──
        std::string d = std::format("  {} {}, {}",
            MON[c.lt.tm_mon + 1], c.lt.tm_mday, c.lt.tm_year + 1900);
        p.text_over(x + 1 + cw, y, d, ink, soft, true);
        (void)bg;
    }
    int size_ = 0;   // 0 huge, 1 big, 2 compact
    long hm_key_ = LONG_MIN;            // signature of the cached HH:MM raster (1/min)
    long ss_key_ = LONG_MIN;            // signature of the cached seconds raster (1/sec)
    std::vector<CapCell> hm_cells_;     // captured HH:MM glyph cells, replayed/frame
    std::vector<CapCell> ss_cells_;     // captured seconds glyph cells, replayed/frame
};

} // namespace chronos::ui
