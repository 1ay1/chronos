// chronos — a beautiful terminal time/calendar/sky dashboard.
//
// Calendar · world clocks · sun & moon · countdowns — all in one pretty panel.
// Built on maya (https://github.com/1ay1/maya).
//
//   h/l  prev/next month     g/G  prev/next year
//   t    jump to today       a    toggle ASCII art clock
//   q    quit
//
// Location for sun/moon defaults to env CHRONOS_LAT / CHRONOS_LON
// (falls back to London). Set them for accurate sunrise/sunset.

#include <maya/maya.hpp>
#include <maya/widget/divider.hpp>

#include "astro.hpp"
#include "timeutil.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <format>
#include <string>
#include <vector>

using namespace maya;
using namespace maya::dsl;
using namespace chronos;

// ── palette ──────────────────────────────────────────────────────────────
namespace pal {
constexpr auto bg_panel = 0x16161e;
constexpr Color text    = Color::hex(0xc0caf5);
constexpr Color muted   = Color::hex(0x565f89);
constexpr Color faint   = Color::hex(0x3b4261);
constexpr Color accent  = Color::hex(0x7aa2f7);  // blue
constexpr Color violet  = Color::hex(0xbb9af7);
constexpr Color cyan    = Color::hex(0x7dcfff);
constexpr Color green   = Color::hex(0x9ece6a);
constexpr Color yellow  = Color::hex(0xe0af68);
constexpr Color orange  = Color::hex(0xff9e64);
constexpr Color red     = Color::hex(0xf7768e);
constexpr Color teal    = Color::hex(0x73daca);
constexpr Color border  = Color::hex(0x29304a);
constexpr Color sun      = Color::hex(0xffd479);
constexpr Color moon     = Color::hex(0xc8d3f5);
} // namespace pal

// ── big seven-segment digits for the hero clock ───────────────────────────
namespace bigfont {
// 5 rows tall, glyphs for 0-9 and ':'
static const std::array<std::array<const char*, 5>, 11> glyphs = {{
    {{" ██ ", "█  █", "█  █", "█  █", " ██ "}},  // 0
    {{"  █ ", " ██ ", "  █ ", "  █ ", " ███"}},  // 1
    {{" ██ ", "█  █", "  █ ", " █  ", "████"}},  // 2
    {{"███ ", "   █", " ██ ", "   █", "███ "}},  // 3
    {{"█  █", "█  █", "████", "   █", "   █"}},  // 4
    {{"████", "█   ", "███ ", "   █", "███ "}},  // 5
    {{" ██ ", "█   ", "███ ", "█  █", " ██ "}},  // 6
    {{"████", "   █", "  █ ", " █  ", " █  "}},  // 7
    {{" ██ ", "█  █", " ██ ", "█  █", " ██ "}},  // 8
    {{" ██ ", "█  █", " ███", "   █", " ██ "}},  // 9
    {{"    ", "  █ ", "    ", "  █ ", "    "}},  // :
}};

inline int idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ':') return 10;
    return -1;
}

// Build the 5 rows of a HH:MM:SS string.
inline std::vector<std::string> render(const std::string& s) {
    std::vector<std::string> rows(5);
    for (char c : s) {
        int g = idx(c);
        for (int r = 0; r < 5; ++r) {
            if (g < 0) rows[r] += "  ";
            else { rows[r] += glyphs[g][r]; rows[r] += ' '; }
        }
    }
    return rows;
}
} // namespace bigfont

// ── date helpers ───────────────────────────────────────────────────────────
namespace cal {
inline bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
inline int days_in(int y, int m) {
    static const int d[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && is_leap(y)) return 29;
    return d[m];
}
// 0=Mon .. 6=Sun for the 1st.
inline int first_dow(int y, int m) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int yy = y; if (m < 3) yy -= 1;
    int dow = (yy + yy/4 - yy/100 + yy/400 + t[m-1] + 1) % 7;
    return (dow + 6) % 7;
}
inline const char* month_name(int m) {
    static const char* n[] = {"","January","February","March","April","May","June",
        "July","August","September","October","November","December"};
    return n[m];
}
} // namespace cal

// ════════════════════════════════════════════════════════════════════════
struct Chronos {
    struct Model {
        std::time_t now = std::time(nullptr);
        int view_year   = 0;   // calendar month being viewed
        int view_month  = 0;
        bool big_clock  = true;
        double lat = 51.5074;  // London default
        double lon = -0.1278;
        int term_w = 80, term_h = 24;
    };

    struct Tick {};
    struct PrevMonth {}; struct NextMonth {};
    struct PrevYear {};  struct NextYear {};
    struct Today {};
    struct ToggleClock {};
    struct Resized { int w, h; };
    struct Quit {};
    using Msg = std::variant<Tick, PrevMonth, NextMonth, PrevYear, NextYear,
                             Today, ToggleClock, Resized, Quit>;

    static Model init() {
        Model m;
        m.now = std::time(nullptr);
        std::tm lt{}; localtime_r(&m.now, &lt);
        m.view_year  = lt.tm_year + 1900;
        m.view_month = lt.tm_mon + 1;
        if (const char* la = std::getenv("CHRONOS_LAT")) m.lat = std::atof(la);
        if (const char* lo = std::getenv("CHRONOS_LON")) m.lon = std::atof(lo);
        return m;
    }

    static auto update(Model m, Msg msg) -> std::pair<Model, Cmd<Msg>> {
        return std::visit(overload{
            [&](Tick)      { m.now = std::time(nullptr); return std::pair{m, Cmd<Msg>{}}; },
            [&](PrevMonth) { if (--m.view_month < 1) { m.view_month = 12; m.view_year--; } return std::pair{m, Cmd<Msg>{}}; },
            [&](NextMonth) { if (++m.view_month > 12) { m.view_month = 1; m.view_year++; } return std::pair{m, Cmd<Msg>{}}; },
            [&](PrevYear)  { m.view_year--; return std::pair{m, Cmd<Msg>{}}; },
            [&](NextYear)  { m.view_year++; return std::pair{m, Cmd<Msg>{}}; },
            [&](Today)     { std::tm lt{}; localtime_r(&m.now, &lt);
                             m.view_year = lt.tm_year + 1900; m.view_month = lt.tm_mon + 1;
                             return std::pair{m, Cmd<Msg>{}}; },
            [&](ToggleClock){ m.big_clock = !m.big_clock; return std::pair{m, Cmd<Msg>{}}; },
            [&](Resized r) { m.term_w = r.w; m.term_h = r.h; return std::pair{m, Cmd<Msg>{}}; },
            [](Quit)       { return std::pair{Model{}, Cmd<Msg>::quit()}; },
        }, msg);
    }

    // ── panel chrome ───────────────────────────────────────────────────────
    static Element panel(const char* title, Color tcol, Element body) {
        return v(
            h(text("▌ ") | fgc(tcol), text(title) | Bold | fgc(tcol)),
            blank_,
            std::move(body)
        ) | padding(1, 2) | border(BorderStyle::Round) | bcolor(pal::border)
          | bgc(Color::hex(pal::bg_panel));
    }

    // ── hero: big clock + date ───────────────────────────────────────────
    static Element hero(const Model& m) {
        std::tm lt{}; localtime_r(&m.now, &lt);
        static const char* wd[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                                   "THURSDAY","FRIDAY","SATURDAY"};
        std::string clock = std::format("{:02}:{:02}:{:02}", lt.tm_hour, lt.tm_min, lt.tm_sec);
        std::string bigclock = std::format("{:02}:{:02}", lt.tm_hour, lt.tm_min);
        std::string datestr = std::format("{} · {} {}, {}",
            wd[lt.tm_wday % 7], cal::month_name(lt.tm_mon + 1), lt.tm_mday, lt.tm_year + 1900);

        std::vector<Element> body;
        if (m.big_clock) {
            auto rows = bigfont::render(bigclock);
            for (auto& r : rows) body.push_back(text(r) | nowrap | fgc(pal::accent));
            body.push_back(text(std::format("        :{:02}", lt.tm_sec)) | nowrap | fgc(pal::muted));
        } else {
            body.push_back(text(clock) | Bold | fgc(pal::accent));
        }

        return v(
            h(text("⟡ ") | fgc(pal::violet),
              text("CHRONOS") | Bold | fgc(pal::violet),
              space,
              text(datestr) | fgc(pal::cyan)) | grow(1),
            blank_,
            v(std::move(body)) | align(Align::Center)
        ) | padding(1, 2) | border(BorderStyle::Round) | bcolor(pal::violet)
          | bgc(Color::hex(pal::bg_panel));
    }

    // ── calendar grid ─────────────────────────────────────────────────────
    static Element calendar(const Model& m) {
        std::tm lt{}; localtime_r(&m.now, &lt);
        int ty = lt.tm_year + 1900, tm = lt.tm_mon + 1, td = lt.tm_mday;

        int y = m.view_year, mo = m.view_month;
        int dim = cal::days_in(y, mo);
        int fdow = cal::first_dow(y, mo);
        bool this_month = (y == ty && mo == tm);

        std::string title = std::format("{} {}", cal::month_name(mo), y);
        int pad = std::max(0, (20 - (int)title.size()) / 2);

        std::vector<Element> rows;
        rows.push_back(text(std::string(pad, ' ') + title) | nowrap | Bold | fgc(pal::yellow));
        rows.push_back(h(
            text("Mo ") | fgc(pal::muted), text("Tu ") | fgc(pal::muted),
            text("We ") | fgc(pal::muted), text("Th ") | fgc(pal::muted),
            text("Fr ") | fgc(pal::muted), text("Sa ") | fgc(pal::red),
            text("Su") | fgc(pal::red)));

        int day = 1;
        while (day <= dim) {
            std::vector<Element> cells;
            for (int col = 0; col < 7; ++col) {
                if ((day == 1 && col < fdow) || day > dim) {
                    cells.push_back(text("   ") | nowrap);
                    continue;
                }
                std::string cell = (day < 10 ? " " : "") + std::to_string(day);
                bool weekend = col >= 5;
                bool today   = this_month && day == td;

                Element e = today
                    ? (text(cell) | Bold | fgc(Color::hex(0x16161e)) | bgc(pal::accent))
                    : weekend ? (text(cell) | fgc(pal::red))
                              : (text(cell) | fgc(pal::text));
                cells.push_back(h(std::move(e), (col < 6 ? text(" ") : text(""))));
                day++;
            }
            rows.push_back(h(std::move(cells)));
        }
        // pad to a stable 6-week height so layout doesn't jiggle.
        while (rows.size() < 8) rows.push_back(blank_);

        return panel("CALENDAR", pal::yellow, v(std::move(rows)));
    }

    // ── world clocks ──────────────────────────────────────────────────────
    static Element clocks(const Model& m) {
        auto zones = timeutil::default_zones();
        // Put Local first.
        std::tm lt{}; localtime_r(&m.now, &lt);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
        std::vector<Element> rows;

        auto fmt_row = [](const std::string& label, int hh, int mm,
                          const std::string& wday, const std::string& mabbr, int d,
                          bool is_day, bool local) {
            Color lc = local ? pal::teal : pal::cyan;
            std::string sun = is_day ? "\xE2\x98\x80" : "\xE2\x98\xBD";  // ☀ / ☽
            return h(
                text(std::format("{:<6}", label)) | Bold | fgc(lc),
                text(sun + " ") | fgc(is_day ? pal::sun : pal::muted),
                text(std::format("{:02}:{:02}", hh, mm)) | fgc(pal::text),
                text("  ") ,
                text(std::format("{} {} {}", wday, d, mabbr)) | fgc(pal::muted)
            );
        };

        rows.push_back(fmt_row("Local", lt.tm_hour, lt.tm_min, wd[lt.tm_wday % 7],
                               mo[lt.tm_mon % 12], lt.tm_mday,
                               lt.tm_hour >= 6 && lt.tm_hour < 19, true));
        for (auto& z : zones) {
            auto r = timeutil::read_zone(m.now, z);
            rows.push_back(fmt_row(r.label, r.hour, r.minute, r.weekday,
                                   r.month_abbr, r.day, r.is_day, false));
        }
        return panel("WORLD CLOCKS", pal::cyan, v(std::move(rows)));
    }

    // ── sky: sun arc + daylight + moon ────────────────────────────────────
    static Element sky(const Model& m) {
        double tz = timeutil::local_utc_offset_hours(m.now);
        std::tm lt{}; localtime_r(&m.now, &lt);
        auto st = astro::sun_times(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                                   m.lat, m.lon, tz);
        auto mp = astro::moon_phase(m.now);

        auto hm = [](double h) {
            int hh = (int)h % 24; int mm = (int)std::round((h - std::floor(h)) * 60);
            if (mm == 60) { mm = 0; hh = (hh + 1) % 24; }
            return std::format("{:02}:{:02}", hh, mm);
        };

        std::vector<Element> rows;
        if (st.valid && !st.always_up && !st.always_down) {
            double now_h = lt.tm_hour + lt.tm_min / 60.0;
            // sun progress across the day arc
            double frac = (now_h - st.sunrise_h) / std::max(0.01, st.daylight_h);
            frac = std::clamp(frac, 0.0, 1.0);
            bool is_day = now_h >= st.sunrise_h && now_h <= st.sunset_h;

            // arc: 18 cells, sun glyph rides on top
            const int W = 18;
            int pos = (int)std::round(frac * (W - 1));
            std::string arc;
            for (int i = 0; i < W; ++i) {
                if (is_day && i == pos) arc += "\xE2\x98\x80";          // ☀
                else if (i < pos && is_day) arc += "\xC2\xB7";          // ·
                else arc += "\xE2\x80\xBE";                             // ‾ overline (sky)
            }
            int dh = (int)st.daylight_h;
            int dm = (int)std::round((st.daylight_h - dh) * 60);

            rows.push_back(h(text("\xE2\x86\x91 ") | fgc(pal::sun),
                             text(hm(st.sunrise_h)) | fgc(pal::orange),
                             space,
                             text(hm(st.sunset_h)) | fgc(pal::violet),
                             text(" \xE2\x86\x93") | fgc(pal::violet)));
            rows.push_back(text(arc) | nowrap | fgc(is_day ? pal::sun : pal::faint));
            rows.push_back(h(text("daylight ") | fgc(pal::muted),
                             text(std::format("{}h {:02}m", dh, dm)) | Bold | fgc(pal::yellow)));
        } else if (st.always_up) {
            rows.push_back(text("\xE2\x98\x80 midnight sun \xC2\xB7 24h daylight") | nowrap | fgc(pal::sun));
        } else if (st.always_down) {
            rows.push_back(text("\xE2\x98\xBD polar night \xC2\xB7 no sunrise") | nowrap | fgc(pal::violet));
        } else {
            rows.push_back(text("set CHRONOS_LAT / CHRONOS_LON") | nowrap | fgc(pal::muted));
        }

        rows.push_back(blank_);

        // moon: illumination bar
        const int MW = 14;
        int fill = (int)std::round(mp.illum * MW);
        std::string bar;
        for (int i = 0; i < MW; ++i) bar += (i < fill ? "\xE2\x96\x88" : "\xE2\x96\x91");
        rows.push_back(h(text(mp.glyph + std::string(" ")) ,
                         text(mp.name) | Bold | fgc(pal::moon)));
        rows.push_back(h(text(bar + " ") | fgc(pal::moon),
                         text(std::format("{:.0f}%", mp.illum * 100)) | fgc(pal::muted)));

        return panel("SKY", pal::orange, v(std::move(rows)));
    }

    // ── upcoming events countdown ─────────────────────────────────────────
    static Element upcoming(const Model& m) {
        auto evs = timeutil::upcoming_events(m.now);
        std::vector<Element> rows;
        Color cyc[] = {pal::green, pal::cyan, pal::violet, pal::orange};
        int i = 0;
        for (auto& e : evs) {
            Color c = cyc[i++ % 4];
            std::string when = e.days == 0 ? "today!"
                             : e.days == 1 ? "tomorrow"
                             : std::format("{} days", e.days);
            rows.push_back(h(
                text(e.glyph + std::string("  ")),
                text(std::format("{:<9}", when)) | Bold | fgc(c),
                text(e.name) | fgc(pal::text)
            ));
        }
        return panel("UPCOMING", pal::green, v(std::move(rows)));
    }

    // ── footer ────────────────────────────────────────────────────────────
    static Element footer() {
        auto k = [](const char* key, const char* desc) {
            return h(text(key) | Bold | fgc(pal::accent),
                     text(std::string(" ") + desc) | fgc(pal::muted),
                     text("   "));
        };
        return h(
            k("h/l", "month"), k("g/G", "year"), k("t", "today"),
            k("a", "clock"), k("q", "quit")
        ) | padding(0, 1);
    }

    // ── view ──────────────────────────────────────────────────────────────
    static Element view(const Model& m) {
        // left column: hero + calendar.  right column: clocks + sky + upcoming.
        auto left = v(
            hero(m),
            calendar(m)
        ) | gap(1) | width(44);

        auto right = v(
            clocks(m),
            sky(m),
            upcoming(m)
        ) | gap(1) | width(36);

        return v(
            h(std::move(left), std::move(right)) | gap(2) | align(Align::Start) | grow(1),
            footer()
        ) | padding(1, 2) | gap(1);
    }

    static auto subscribe(const Model&) -> Sub<Msg> {
        return Sub<Msg>::batch(
            Sub<Msg>::every(std::chrono::seconds(1), Tick{}),
            key_map<Msg>({
                {'q', Quit{}}, {'h', PrevMonth{}}, {'l', NextMonth{}},
                {'g', PrevYear{}}, {'G', NextYear{}}, {'t', Today{}},
                {'a', ToggleClock{}},
            }),
            Sub<Msg>::on_key([](const KeyEvent& ke) -> std::optional<Msg> {
                if (auto* sk = std::get_if<SpecialKey>(&ke.key)) {
                    if (*sk == SpecialKey::Left)  return PrevMonth{};
                    if (*sk == SpecialKey::Right) return NextMonth{};
                    if (*sk == SpecialKey::Up)    return PrevYear{};
                    if (*sk == SpecialKey::Down)  return NextYear{};
                    if (*sk == SpecialKey::Escape) return Quit{};
                }
                return std::nullopt;
            }),
            Sub<Msg>::on_resize([](Size sz) -> Msg {
                return Resized{sz.width.value, sz.height.value};
            })
        );
    }
};

static_assert(Program<Chronos>);

int main() {
    run<Chronos>({.title = "chronos"});
}
