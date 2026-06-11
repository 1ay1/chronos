// Headless layout check for the sun / moon / weather dashboard cards.
// Renders the three cards to an off-screen maya Canvas and dumps the glyph
// grid as ASCII so we can sanity-check positions and labels without a TTY.
//
//   ./cardpreview [card_w] [card_h]   (default 30 x 8)
//
// Builds the same synthetic Ctx the app uses, with a forced weather snapshot.

#include <maya/internal.hpp>
#include "../src/gfx.hpp"
#include "../src/widget.hpp"
#include "../src/widgets/sun.hpp"
#include "../src/widgets/moon.hpp"
#include "../src/widgets/weather.hpp"

#include <cstdio>
#include <ctime>

using namespace maya;
namespace ui = chronos::ui;

int main(int argc, char** argv) {
    int cw = argc > 1 ? atoi(argv[1]) : 30;
    int ch = argc > 2 ? atoi(argv[2]) : 8;
    int gap = 2;
    int W = cw * 3 + gap * 2 + 2;
    int H = ch + 2;

    StylePool pool;
    Canvas cv(W, H, &pool);
    chronos::gfx::Painter p(cv, pool, W, H);

    ui::Ctx c;
    c.now = std::time(nullptr);
    if (argc > 4) {
        // force a specific local hour for daytime/night testing
        std::tm t{}; localtime_r(&c.now, &t);
        t.tm_hour = atoi(argv[4]); t.tm_min = 0; t.tm_sec = 0;
        c.now = std::mktime(&t);
    }
    localtime_r(&c.now, &c.lt);
    c.theme = ui::tokyo_night();
    c.lat = 51.5; c.lon = -0.12;
    c.tz_offset = 0;
    c.sun  = chronos::astro::sun_position(c.now, c.lat, c.lon);
    c.moon = chronos::astro::moon_phase(c.now);
    c.sun_times = chronos::astro::sun_times(
        c.lt.tm_year + 1900, c.lt.tm_mon + 1, c.lt.tm_mday, c.lat, c.lon, c.tz_offset);

    c.weather.valid    = true;
    c.weather.temp_c   = 14;
    c.weather.feels_c  = 12;
    c.weather.hi_c     = 18;
    c.weather.lo_c     = 9;
    c.weather.humidity = 72;
    c.weather.wind_kmh = 14;
    c.weather.wind_dir = 225;
    c.weather.code     = argc > 3 ? atoi(argv[3]) : 2;
    c.weather.is_day   = c.sun.altitude > 0;
    c.weather.fetched  = c.now - 25;

    ui::SunArcWidget sun;
    ui::MoonWidget   moon;
    ui::WeatherWidget weather;

    int x = 1;
    sun.paint(p,     ui::Rect{x, 1, cw, ch}, c); x += cw + gap;
    moon.paint(p,    ui::Rect{x, 1, cw, ch}, c); x += cw + gap;
    weather.paint(p, ui::Rect{x, 1, cw, ch}, c);

    for (int y = 0; y < H; ++y) {
        std::string line;
        for (int xx = 0; xx < W; ++xx) {
            char32_t g = cv.get(xx, y).character;
            if (g == 0 || g == U' ') { line += ' '; continue; }
            if (g < 128) { line += (char)g; continue; }
            switch (g) {
                case U'\u2500': line += '-'; break;
                case U'\u2502': line += '|'; break;
                case U'\u2580': line += ' '; break;   // half-block bg
                case U'\u2588': line += '#'; break;
                case U'\u2591': line += '.'; break;
                case U'\u256d': case U'\u256e':
                case U'\u2570': case U'\u256f': line += '+'; break;
                case U'\u00b7': line += '.'; break;
                case U'\u00b0': line += 'o'; break;
                case U'\u2191': line += '^'; break;
                case U'\u2193': line += 'v'; break;
                case U'\u2248': line += '~'; break;
                case U'\u25cb': line += 'O'; break;
                case U'\u25cf': line += 'o'; break;
                case U'\u25b2': line += '^'; break;
                case U'\u25bc': line += 'v'; break;
                default:
                    if (g >= U'\u2580' && g <= U'\u259f') { line += ' '; break; }
                    line += '?';
            }
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        std::printf("%s\n", line.c_str());
    }
    return 0;
}
