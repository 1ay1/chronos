#pragma once
// chronos::ui — widget framework.
//
// A Widget is a self-contained graphical component that knows how to paint
// itself into a Rect and (optionally) handle a key. Everything chronos shows
// — the sky, the clock, the calendar, the moon — is a Widget. The app owns a
// list of them, lays them out, and dispatches paint/key.

#include <maya/internal.hpp>

#include "gfx.hpp"
#include "astro.hpp"
#include "weather.hpp"

#include <ctime>
#include <string>

namespace chronos::ui {

using gfx::Col;
using gfx::Painter;

// ── rectangle in terminal-cell space ────────────────────────────────────────
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] int right()  const { return x + w; }
    [[nodiscard]] int bottom() const { return y + h; }
    [[nodiscard]] bool contains(int cx, int cy) const {
        return cx >= x && cx < x + w && cy >= y && cy < y + h;
    }
    [[nodiscard]] Rect inset(int p) const { return {x + p, y + p, w - 2 * p, h - 2 * p}; }
};

// ── theme: the few colours widgets share ────────────────────────────────────
struct Theme {
    Col text, text_dim, accent, warm, cool, good, warn, bad;
    Col panel_bg, panel_border, scrim;
};
inline Theme tokyo_night() {
    return Theme{
        .text         = gfx::hex(0xc0caf5),
        .text_dim     = gfx::hex(0x565f89),
        .accent       = gfx::hex(0x7aa2f7),
        .warm         = gfx::hex(0xff9e64),
        .cool         = gfx::hex(0x7dcfff),
        .good         = gfx::hex(0x9ece6a),
        .warn         = gfx::hex(0xe0af68),
        .bad          = gfx::hex(0xf7768e),
        .panel_bg     = gfx::hex(0x10111a),
        .panel_border = gfx::hex(0x3b4261),
        .scrim        = gfx::hex(0x05050a),
    };
}

// ── per-frame context handed to every widget ────────────────────────────────
struct Ctx {
    std::time_t now = 0;          // simulated "now" (may be time-warped)
    std::tm     lt{};             // localtime(now)
    double      lat = 0, lon = 0;
    std::string place;
    float       anim = 0;         // free-running clock for animations
    double      warp_rate = 0;    // current time-warp speed (0 = live)
    double      time_warp = 0;    // accumulated warp offset (seconds)
    Theme       theme;

    // shared astronomy, computed once per frame
    chronos::astro::SunPos   sun{};
    chronos::astro::MoonPhase moon{};
    chronos::astro::SunTimes sun_times{};
    double tz_offset = 0;

    // live weather (Open-Meteo), refreshed off-thread; valid==false until the
    // first successful fetch lands.
    chronos::weather::Weather weather{};
};

// ── widget base ──────────────────────────────────────────────────────────────
class Widget {
public:
    virtual ~Widget() = default;
    virtual void paint(Painter& p, const Rect& r, const Ctx& c) = 0;
    // return true if the key was consumed
    virtual bool on_key(const maya::Event&) { return false; }
    [[nodiscard]] virtual const char* name() const { return "widget"; }
};

} // namespace chronos::ui
