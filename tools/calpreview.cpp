// Headless layout check for the full-screen calendar. Renders the calendar
// to an off-screen maya Canvas and dumps the character grid as ASCII so we
// can verify positions/labels without a TTY. (Colours are not shown.)
//
//   c++ -std=c++23 -O2 -I maya/include tools/calpreview.cpp maya/.../libmaya.a ...
// Simpler: compile against the same flags the app uses. We instantiate the
// widget directly and drive it with a synthetic Ctx.

#include <maya/internal.hpp>
#include "../src/gfx.hpp"
#include "../src/widget.hpp"
#include "../src/widgets/calendar.hpp"

#include <cstdio>
#include <ctime>

using namespace maya;
namespace ui = chronos::ui;

int main(int argc, char** argv) {
    int W = argc > 1 ? atoi(argv[1]) : 100;
    int H = argc > 2 ? atoi(argv[2]) : 30;

    StylePool pool;
    Canvas cv(W, H, &pool);
    chronos::gfx::Painter p(cv, pool, W, H);

    ui::Ctx c;
    c.now = std::time(nullptr);
    localtime_r(&c.now, &c.lt);
    c.theme = ui::tokyo_night();
    c.lat = 51.5; c.lon = -0.12;
    c.sun  = chronos::astro::sun_position(c.now, c.lat, c.lon);
    c.moon = chronos::astro::moon_phase(c.now);

    ui::CalendarWidget cal;
    int steps = argc > 3 ? atoi(argv[3]) : 0;
    for (int i = 0; i < steps; ++i) {
        maya::Event ev = maya::KeyEvent{ maya::CharKey{U'j'}, {}, {} };
        cal.on_key(ev);
    }
    cal.paint(p, ui::Rect{0,0,W,H}, c);

    // dump the glyph grid (replace box-drawing/wide glyphs with stand-ins)
    for (int y = 0; y < H; ++y) {
        std::string line;
        for (int x = 0; x < W; ++x) {
            char32_t ch = cv.get(x, y).character;
            if (ch == 0 || ch == U' ') { line += ' '; continue; }
            if (ch < 128) { line += (char)ch; continue; }
            // map common glyphs to ascii
            switch (ch) {
                case U'\u2500': line += '-'; break;
                case U'\u2502': line += '|'; break;
                case U'\u256d': case U'\u256e':
                case U'\u2570': case U'\u256f': line += '+'; break;
                case U'\u00b7': line += '.'; break;
                case U'\u25cf': line += 'o'; break;
                case U'\u25cb': line += 'O'; break;
                case U'\u25d0': line += '('; break;
                case U'\u25d1': line += 'D'; break;
                default: line += '#'; break;  // big-font octants & emoji
            }
        }
        // rstrip
        while (!line.empty() && line.back()==' ') line.pop_back();
        std::printf("%s\n", line.c_str());
    }
    return 0;
}
