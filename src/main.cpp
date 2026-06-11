// chronos — a graphical weather & clock app for the terminal.
//
// A living sky drawn pixel-by-pixel, with every section built as a self-
// contained graphical widget (see src/widgets/). Driven by the real local
// time and your location.
//
//   a  clock style   c  calendar   w  world clocks
//   +/- time warp     0  live       q  quit
//
// Location:  export CHRONOS_LAT / CHRONOS_LON / CHRONOS_PLACE  (default London)

#include <maya/internal.hpp>

#include "gfx.hpp"
#include "widget.hpp"
#include "astro.hpp"
#include "timeutil.hpp"
#include "weather.hpp"
#include "geo.hpp"

#include "widgets/sky.hpp"
#include "widgets/clock.hpp"
#include "widgets/location.hpp"
#include "widgets/sun.hpp"
#include "widgets/moon.hpp"
#include "widgets/weather.hpp"
#include "widgets/calendar.hpp"
#include "widgets/clocks.hpp"
#include "widgets/statusbar.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <memory>

using namespace maya;
namespace ui = chronos::ui;

// ════════════════════════════════════════════════════════════════════════
//  App — owns the widgets, builds per-frame Ctx, lays out, routes events
// ════════════════════════════════════════════════════════════════════════
class App {
public:
    App() {
        theme_ = ui::tokyo_night();
        if (const char* la = std::getenv("CHRONOS_LAT")) { lat_ = std::atof(la); have_lat_ = true; }
        if (const char* lo = std::getenv("CHRONOS_LON")) { lon_ = std::atof(lo); have_lon_ = true; }
        if (const char* pl = std::getenv("CHRONOS_PLACE")) { place_ = pl; have_place_ = true; }
        // optional: force a weather code to preview a scene (clear=0, overcast=3,
        // fog=45, rain=61/63/65, snow=71/73/75, thunderstorm=95/99).
        if (const char* wc = std::getenv("CHRONOS_WX_CODE")) wx_force_code_ = std::atoi(wc);
        if (const char* ww = std::getenv("CHRONOS_WX_WIND")) wx_force_wind_ = std::atof(ww);

        sky_      = std::make_unique<ui::SkyWidget>();
        clock_    = std::make_unique<ui::ClockWidget>();
        location_ = std::make_unique<ui::LocationWidget>();
        sun_      = std::make_unique<ui::SunArcWidget>();
        moon_     = std::make_unique<ui::MoonWidget>();
        weather_  = std::make_unique<ui::WeatherWidget>();
        calendar_ = std::make_unique<ui::CalendarWidget>();
        clocks_   = std::make_unique<ui::WorldClocksWidget>();
        statusbar_= std::make_unique<ui::StatusBarWidget>();

        // kick off the first real weather fetch for our location
        wx_.configure(lat_, lon_);

        // Auto-locate via public IP when the user hasn't pinned coordinates.
        // The lookup runs off-thread; tick() applies the result when it lands
        // and re-points the weather feed at the discovered location.
        if (!have_lat_ || !have_lon_) geo_.start();
    }

    void set_pool(StylePool& pool) { pool_ = &pool; }

    bool on_event(const Event& ev) {
        if (key(ev, 'q') || key(ev, SpecialKey::Escape)) return false;
        if (key(ev, 'c')) { show_calendar_ = !show_calendar_; show_clocks_ = false; sky_->invalidate(); return true; }
        if (key(ev, 'w')) { show_clocks_ = !show_clocks_; show_calendar_ = false; sky_->invalidate(); return true; }
        if (key(ev, '0')) { time_warp_ = 0; warp_rate_ = 0; sky_->invalidate(); return true; }
        if (key(ev, '+') || key(ev, '=')) { warp_rate_ = warp_rate_ <= 0 ? 600 : warp_rate_ * 2; sky_->invalidate(); return true; }
        if (key(ev, '-') || key(ev, '_')) { warp_rate_ = warp_rate_ >= 0 ? -600 : warp_rate_ * 2; sky_->invalidate(); return true; }
        // route to the active overlay first, then always-on widgets
        if (show_calendar_ && calendar_->on_key(ev)) { sky_->invalidate(); return true; }
        if (clock_->on_key(ev)) { sky_->invalidate(); return true; }
        return true;
    }

    void tick(float dt) {
        anim_ += dt;
        time_warp_ += warp_rate_ * dt;
        // apply an auto-located position the moment it arrives (once).
        if (auto loc = geo_.take()) {
            if (!have_lat_) lat_ = loc->lat;
            if (!have_lon_) lon_ = loc->lon;
            if (!have_place_ && !loc->place.empty()) place_ = loc->place;
            wx_.configure(lat_, lon_);   // re-point the weather feed
            sky_->invalidate();
        }
        wx_.tick();   // refreshes live weather off-thread when due
    }

    void paint(Canvas& cv, int W, int H) {
        if (!pool_ || W < 24 || H < 10) return;
        // StylePool backstop: maya's pool caps at 65535 unique styles and never
        // evicts — at saturation every new style collapses to terminal-default
        // and the screen corrupts irreversibly (full-day warps sweep enough
        // palette to get there). chronos repaints EVERY cell EVERY frame (the
        // sky blits the whole grid, widgets paint over it), so resetting the
        // pool between frames is safe: all cells re-intern fresh ids this very
        // frame. Cost: one full wire re-emit on the next diff. The sky's
        // adaptive quantization makes this rare; this is the hard guarantee.
        if (pool_->size() > 58000) {
            pool_->clear();
            // Salt: shift id assignment by a varying amount each clear, so an
            // old cell's (char, id) can't coincidentally equal the new frame's
            // (char, id) with a DIFFERENT colour — packed-equal cells are
            // skipped by maya's diff and would display stale colours forever.
            int salt = (++pool_clears_ % 7) + 1;
            for (int i = 0; i < salt; ++i)
                (void)pool_->intern(maya::Style{}.with_fg(
                    maya::Color::rgb(uint8_t(i * 31 + 1), uint8_t(i * 17 + 3), 0)));
            sky_->invalidate();
        }
        chronos::gfx::Painter p(cv, *pool_, W, H);

        // ── build per-frame context ────────────────────────────────────────
        ui::Ctx c;
        c.now = std::time(nullptr) + (std::time_t)time_warp_;
        localtime_r(&c.now, &c.lt);
        c.lat = lat_; c.lon = lon_; c.place = place_;
        c.anim = anim_; c.warp_rate = warp_rate_; c.time_warp = time_warp_;
        c.theme = theme_;
        c.sun  = chronos::astro::sun_position(c.now, lat_, lon_);
        c.moon = chronos::astro::moon_phase(c.now);
        c.tz_offset = chronos::timeutil::local_utc_offset_hours(c.now);
        c.sun_times = chronos::astro::sun_times(
            c.lt.tm_year + 1900, c.lt.tm_mon + 1, c.lt.tm_mday, lat_, lon_, c.tz_offset);
        c.weather = wx_.snapshot();
        // preview override: stamp a forced condition so any scene can be seen
        // on demand (CHRONOS_WX_CODE / CHRONOS_WX_WIND).
        if (wx_force_code_ >= 0) {
            c.weather.valid = true;
            c.weather.code  = wx_force_code_;
            c.weather.wind_kmh = wx_force_wind_;
            c.weather.is_day = c.sun.altitude > 0;
        }

        // ── layout ──────────────────────────────────────────────────────────
        ui::Rect full{0, 0, W, H};
        ui::Rect bar{0, H - 1, W, 1};
        ui::Rect content{0, 0, W, H - 1};

        // background
        sky_->paint(p, full, c);

        // top HUD: clock left, location right
        clock_->paint(p, {2, 1, W - 4, 11}, c);
        location_->paint(p, {2, 1, W - 4, 2}, c);

        // bottom dashboard cards: sun arc, moon, and live weather. They sit
        // side by side; on a narrow terminal there isn't room for three, so we
        // drop down to sun + weather (the two most useful), then sun alone.
        int card_h = std::min(8, std::max(6, (H - 12) / 2 + 4));
        int card_y = content.bottom() - card_h - 1;
        if (card_y > 9) {
            int gap = 2, side = 4;
            int avail = W - side * 2;
            if (avail >= 78) {
                // three cards: sun | moon | weather
                int inner = avail - gap * 2;
                int sun_w = std::clamp(inner * 4 / 10, 24, 46);
                int wx_w  = std::clamp(inner * 3 / 10, 24, 40);
                int moon_w = std::max(20, inner - sun_w - wx_w);
                int x = side;
                sun_->paint(p,     {x, card_y, sun_w, card_h}, c); x += sun_w + gap;
                moon_->paint(p,    {x, card_y, moon_w, card_h}, c); x += moon_w + gap;
                weather_->paint(p, {x, card_y, wx_w, card_h}, c);
            } else if (avail >= 52) {
                // two cards: sun | weather
                int inner = avail - gap;
                int sun_w = std::clamp(inner * 5 / 9, 24, 48);
                int wx_w  = std::max(22, inner - sun_w);
                sun_->paint(p,     {side, card_y, sun_w, card_h}, c);
                weather_->paint(p, {side + sun_w + gap, card_y, wx_w, card_h}, c);
            } else {
                // one card: weather (real data wins the scarce space)
                weather_->paint(p, {side, card_y, avail, card_h}, c);
            }
        }

        // full-screen calendar overlay
        if (show_calendar_) {
            calendar_->paint(p, full, c);
            return;   // calendar owns the whole screen (incl. its own footer)
        }
        if (show_clocks_) {
            int rows = (int)chronos::timeutil::default_zones().size() + 4;
            int pw = 32, ph = rows + 1;
            clocks_->paint(p, {(W - pw) / 2, (H - ph) / 2, pw, ph}, c);
        }

        // status bar
        statusbar_->paint(p, bar, c);
    }

private:
    StylePool* pool_ = nullptr;
    ui::Theme  theme_;
    double lat_ = 51.5074, lon_ = -0.1278;
    int    pool_clears_ = 0;   // style-pool resets this session (salts id shift)
    std::string place_ = "London";
    bool   have_lat_ = false, have_lon_ = false, have_place_ = false;  // pinned via env
    double time_warp_ = 0, warp_rate_ = 0;
    float  anim_ = 0;
    bool   show_calendar_ = false, show_clocks_ = false;
    int    wx_force_code_ = -1;       // CHRONOS_WX_CODE preview override (-1 = live)
    double wx_force_wind_ = 12.0;     // CHRONOS_WX_WIND preview wind speed (km/h)

    std::unique_ptr<ui::SkyWidget>         sky_;
    std::unique_ptr<ui::ClockWidget>       clock_;
    std::unique_ptr<ui::LocationWidget>    location_;
    std::unique_ptr<ui::SunArcWidget>      sun_;
    std::unique_ptr<ui::MoonWidget>        moon_;
    std::unique_ptr<ui::WeatherWidget>     weather_;
    std::unique_ptr<ui::CalendarWidget>    calendar_;
    std::unique_ptr<ui::WorldClocksWidget> clocks_;
    std::unique_ptr<ui::StatusBarWidget>   statusbar_;

    chronos::weather::WeatherService       wx_;   // live weather (Open-Meteo)
    chronos::geo::GeoService               geo_;  // auto-locate via public IP
};

int main() {
    App app;
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    (void)canvas_run(
        CanvasConfig{.fps = 60, .mouse = false, .mode = Mode::Fullscreen,
                     .auto_clear = false, .title = "chronos"},
        [&](StylePool& pool, int, int) { app.set_pool(pool); },
        [&](const Event& ev) -> bool { return app.on_event(ev); },
        [&](Canvas& cv, int W, int H) {
            auto now = Clock::now();
            float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
            last = now;
            app.tick(dt);
            app.paint(cv, W, H);
        }
    );
    return 0;
}
