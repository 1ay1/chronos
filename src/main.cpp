// chronos — a graphical weather & clock app for the terminal.
//
// A living sky rendered pixel-by-pixel on a half-block canvas: the colour of
// the sky, the position of the sun and moon, the stars, and the clouds are
// all driven by the REAL local time and your location. Watch dawn break,
// noon blaze, dusk glow, and night fall — in your terminal.
//
// Overlaid: a big clock, the date, sun & moon data, and a sliding calendar.
//
//   c   calendar          w   world clocks
//   a   toggle clock       +/- time warp (fast-forward the sky)
//   0   real time          q   quit
//
// Location:  export CHRONOS_LAT / CHRONOS_LON  (defaults to London).

#include <maya/internal.hpp>

#include "astro.hpp"
#include "timeutil.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <string>
#include <thread>
#include <vector>

using namespace maya;
using chronos::astro::SunPos;

// ════════════════════════════════════════════════════════════════════════
//  Colour math
// ════════════════════════════════════════════════════════════════════════
struct Col { float r, g, b; };
static inline Col mix(Col a, Col b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
static inline Col add(Col a, Col b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
static inline Col scale(Col a, float s) { return {a.r * s, a.g * s, a.b * s}; }
static inline float smoothstep(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0 + 1e-9f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static inline float frac(float x) { return x - std::floor(x); }
static inline float hash(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return frac(h);
}
static inline float vnoise(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    float a = hash(ix, iy),       b = hash(ix + 1, iy);
    float c = hash(ix, iy + 1),   d = hash(ix + 1, iy + 1);
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    return mix(Col{a,0,0}, Col{b,0,0}, ux).r * (1 - uy)
         + mix(Col{c,0,0}, Col{d,0,0}, ux).r * uy;
}
static inline float fbm(float x, float y) {
    float v = 0, amp = 0.5f;
    for (int i = 0; i < 4; ++i) { v += vnoise(x, y) * amp; x *= 2.03f; y *= 2.01f; amp *= 0.5f; }
    return v;
}

// ════════════════════════════════════════════════════════════════════════
//  Per-frame quantized style cache  (RGB → interned style id)
// ════════════════════════════════════════════════════════════════════════
// A sky gradient touches thousands of distinct shades; interning each on the
// hot path is fine (open-addressed hash) but we quantize to keep the pool
// well under the 65535 cap and to share SGR strings between near-equal cells.
namespace stylecache {
constexpr int Q = 32;  // levels per channel → 32³ fg slots possible
static StylePool* g_pool = nullptr;

static inline uint8_t q8(float v) {
    int q = std::clamp(int(v * (Q - 1) + 0.5f), 0, Q - 1);
    return uint8_t(q * 255 / (Q - 1));
}
// Half-block cell: fg = top pixel, bg = bottom pixel.
static inline uint16_t cell(Col top, Col bot) {
    Style s = Style{}
        .with_fg(Color::rgb(q8(top.r), q8(top.g), q8(top.b)))
        .with_bg(Color::rgb(q8(bot.r), q8(bot.g), q8(bot.b)));
    return g_pool->intern(s);
}
static inline uint16_t fg_on(Col fg, Col bg, bool bold = false) {
    Style s = Style{}
        .with_fg(Color::rgb(q8(fg.r), q8(fg.g), q8(fg.b)))
        .with_bg(Color::rgb(q8(bg.r), q8(bg.g), q8(bg.b)));
    if (bold) s = s.with_bold();
    return g_pool->intern(s);
}
} // namespace stylecache

// ════════════════════════════════════════════════════════════════════════
//  Sky model — maps sun altitude + a fragment's screen position to a colour
// ════════════════════════════════════════════════════════════════════════
namespace sky {

// Keyframed horizon/zenith palettes by sun altitude (degrees).
struct Band { float alt; Col zenith; Col horizon; };
static const Band bands[] = {
    {-18.f, {0.01f,0.01f,0.04f}, {0.02f,0.02f,0.07f}},  // astronomical night
    { -8.f, {0.03f,0.04f,0.12f}, {0.10f,0.07f,0.18f}},  // deep twilight
    { -3.f, {0.07f,0.09f,0.22f}, {0.55f,0.28f,0.22f}},  // civil twilight / golden
    {  0.f, {0.16f,0.22f,0.40f}, {0.95f,0.55f,0.30f}},  // sunrise/sunset
    {  6.f, {0.22f,0.40f,0.70f}, {0.85f,0.70f,0.62f}},  // early morning
    { 25.f, {0.18f,0.42f,0.82f}, {0.62f,0.78f,0.95f}},  // mid-day blue
    { 60.f, {0.13f,0.40f,0.88f}, {0.55f,0.78f,0.98f}},  // high noon
};

static Col palette(float alt, float v /*0=horizon..1=zenith*/) {
    const int n = int(sizeof(bands) / sizeof(bands[0]));
    int i = 0;
    while (i < n - 1 && alt > bands[i + 1].alt) ++i;
    float t = (i < n - 1)
        ? smoothstep(bands[i].alt, bands[i + 1].alt, alt)
        : 0.f;
    Col zen = mix(bands[i].zenith,  bands[std::min(i + 1, n - 1)].zenith,  t);
    Col hor = mix(bands[i].horizon, bands[std::min(i + 1, n - 1)].horizon, t);
    // vertical gradient, horizon glow concentrated low
    float g = std::pow(v, 0.65f);
    return mix(hor, zen, g);
}

} // namespace sky

// ════════════════════════════════════════════════════════════════════════
//  App state
// ════════════════════════════════════════════════════════════════════════
struct State {
    double lat = 51.5074, lon = -0.1278;
    std::string place = "London";
    double time_warp = 0;      // seconds of simulated offset added to "now"
    double warp_rate = 0;      // when nonzero, time accelerates
    bool show_calendar = false;
    bool show_clocks   = false;
    bool big_clock     = true;
    int cal_year = 0, cal_month = 0;
    float anim = 0;            // free-running animation clock (clouds, twinkle)
};
static State g;

static std::time_t sim_now() {
    return std::time(nullptr) + (std::time_t)g.time_warp;
}

// forward decls so paint() can call the overlay panels
static void paint_calendar(Canvas&, int, int, const std::tm&);
static void paint_clocks(Canvas&, int, int, std::time_t);

// ════════════════════════════════════════════════════════════════════════
//  HUD overlay glyphs — 5-row seven-segment for the big clock
// ════════════════════════════════════════════════════════════════════════
namespace bigfont {
static const char* G[11][5] = {
    {" ▟█▙ ","█▘ ▝█","█   █","█▖ ▗█"," ▜█▛ "}, // 0
    {"  █  "," ██  ","  █  ","  █  "," ███ "}, // 1
    {" ▟█▙ ","▘  ▝█","  ▟█▘"," ▟▘  ","▟███▖"}, // 2
    {"▟██▙ ","   ▟█"," ▜█▙ ","   ▟█","▜██▛ "}, // 3
    {"█  █ ","█  █ ","▜███▖","   █ ","   █ "}, // 4
    {"████▖","█    ","▜██▙ ","   ▟█","▜██▛ "}, // 5
    {" ▟█▙ ","█▘   ","██▙▖ ","█▘ ▝█"," ▜█▛ "}, // 6
    {"████▌","   ▟▘","  ▟▘ "," ▟▘  "," █   "}, // 7
    {" ▟█▙ ","█▘ ▝█"," ▜█▛ ","█▖ ▗█"," ▜█▛ "}, // 8
    {" ▟█▙ ","█▘ ▝█"," ▜██▌","   ▟█"," ▜█▛ "}, // 9
    {"     ","  ▗  ","     ","  ▗  ","     "}, // :
};
static int idx(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ':') return 10;
    return -1;
}
} // namespace bigfont

// ════════════════════════════════════════════════════════════════════════
//  Paint
// ════════════════════════════════════════════════════════════════════════
static const char* WD_FULL[] = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY",
                                "THURSDAY","FRIDAY","SATURDAY"};
static const char* MON_FULL[] = {"","January","February","March","April","May",
    "June","July","August","September","October","November","December"};
static const char* WD3[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* MON3[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};

// draw a UTF-8 string at (cx,cy) with a foreground colour, sampling the sky
// behind each cell for the background so text floats over the gradient.
static void draw_text_over(Canvas& cv, int cx, int cy, std::string_view s,
                           Col fg, bool bold, float sun_alt, int pixel_h) {
    int x = cx;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        int bytes = 1;
        if (c >= 0xF0) bytes = 4; else if (c >= 0xE0) bytes = 3; else if (c >= 0xC0) bytes = 2;
        std::string ch = std::string(s.substr(i, bytes));
        i += bytes;
        // background = sky colour at this cell's bottom pixel
        float v = 1.f - float(cy * 2 + 1) / float(pixel_h);
        Col bg = sky::palette(sun_alt, v);
        bg = mix(bg, Col{0,0,0}, 0.35f);  // slight scrim for legibility
        uint16_t st = stylecache::fg_on(fg, bg, bold);
        char32_t cp = 0;
        // decode minimal codepoint for canvas (handles ascii + the glyphs we use)
        if (bytes == 1) cp = ch[0];
        else {
            // decode utf-8
            unsigned char b0 = ch[0];
            if (bytes == 2) cp = ((b0 & 0x1F) << 6) | (ch[1] & 0x3F);
            else if (bytes == 3) cp = ((b0 & 0x0F) << 12) | ((ch[1] & 0x3F) << 6) | (ch[2] & 0x3F);
            else cp = ((b0 & 0x07) << 18) | ((ch[1] & 0x3F) << 12) | ((ch[2] & 0x3F) << 6) | (ch[3] & 0x3F);
        }
        cv.set(x, cy, cp, st);
        x++;
    }
}

// Draw plain text into a solid status bar (no sky sampling).
static void draw_bar_text(Canvas& cv, int cx, int by, std::string_view s,
                          Col fg, Col bg) {
    int x = cx;
    size_t i = 0;
    uint16_t st = stylecache::fg_on(fg, bg);
    while (i < s.size()) {
        unsigned char c = s[i];
        int bytes = 1;
        if (c >= 0xF0) bytes = 4; else if (c >= 0xE0) bytes = 3; else if (c >= 0xC0) bytes = 2;
        char32_t cp = 0;
        if (bytes == 1) cp = c;
        else if (bytes == 2) cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F);
        else if (bytes == 3) cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F);
        else cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F);
        cv.set(x, by, cp, st);
        i += bytes; x++;
    }
}

// Everything the per-pixel shader needs for one frame.
struct SkyCtx {
    float sun_x, sun_y, moon_x, moon_y;
    float sun_alt, horizon_y, star_vis, anim;
    float moon_frac;
    int pixel_w, pixel_h;
    bool is_night;
};

static Col shade_pixel(const SkyCtx& s, float px, float py) {
    Col c;
    float vv = 1.f - py / float(s.pixel_h);
    if (py < s.horizon_y) {
        c = sky::palette(s.sun_alt, vv);

        // Sun disc + glow
        float dxs = px - s.sun_x, dys = py - s.sun_y;
        float ds = std::sqrt(dxs * dxs + dys * dys);
        if (s.sun_alt > -2.f) {
            float disc = smoothstep(3.2f, 2.0f, ds);
            float glow = smoothstep(s.pixel_w * 0.5f, 0.f, ds) * 0.45f;
            Col suncol = mix(Col{1.0f,0.95f,0.75f}, Col{1.0f,0.78f,0.45f},
                             smoothstep(20.f, 0.f, s.sun_alt));
            c = add(c, scale(suncol, glow));
            c = mix(c, Col{1.0f,0.98f,0.9f}, disc);
        }

        // Moon disc + soft glow
        if (s.is_night || s.sun_alt < 6.f) {
            float dxm = px - s.moon_x, dym = py - s.moon_y;
            float dm = std::sqrt(dxm * dxm + dym * dym);
            float mvis = smoothstep(0.f, -4.f, s.sun_alt);
            float disc = smoothstep(2.6f, 1.6f, dm) * mvis;
            float glow = smoothstep(10.f, 0.f, dm) * 0.18f * mvis;
            Col mooncol{0.86f, 0.88f, 0.95f};
            c = add(c, scale(mooncol, glow));
            float lit = std::cos((s.moon_frac - 0.5f) * 2.f * 3.14159f);
            float limb = dxm / 2.0f;
            float shadow = smoothstep(-lit, -lit + 0.6f, limb);
            Col lunar = mix(scale(mooncol, 0.25f), mooncol, shadow);
            c = mix(c, lunar, disc);
        }

        // Stars
        if (s.star_vis > 0.01f && py < s.horizon_y * 0.95f) {
            float gx = std::floor(px), gy = std::floor(py);
            float hs = hash(gx * 1.7f, gy * 2.3f);
            if (hs > 0.985f) {
                float tw = 0.6f + 0.4f * std::sin(s.anim * 3.f + hs * 50.f);
                float br = (hs - 0.985f) / 0.015f * tw * s.star_vis;
                c = add(c, scale(Col{0.9f,0.92f,1.0f}, br));
            }
        }

        // Clouds
        float cloud_band = smoothstep(s.horizon_y, s.horizon_y * 0.25f, py);
        float n = fbm(px * 0.05f + s.anim * 0.06f, py * 0.10f + 12.3f);
        float cl = smoothstep(0.55f, 0.78f, n) * cloud_band;
        if (cl > 0.01f) {
            float daylight = smoothstep(-4.f, 8.f, s.sun_alt);
            Col cloud_lit  = Col{0.95f,0.95f,0.98f};
            Col cloud_dusk = Col{0.85f,0.55f,0.45f};
            Col cloud_night= Col{0.10f,0.11f,0.18f};
            Col cc = mix(cloud_night, mix(cloud_dusk, cloud_lit, daylight),
                         smoothstep(-10.f, 2.f, s.sun_alt));
            c = mix(c, cc, cl * 0.85f);
        }
    } else {
        // Ground silhouette
        float hill = s.horizon_y
            + 3.f * std::sin(px * 0.07f)
            + 2.f * std::sin(px * 0.17f + 1.3f);
        Col sky_at_horizon = sky::palette(s.sun_alt, 0.f);
        Col ground = scale(sky_at_horizon, 0.18f);
        ground = mix(ground, Col{0.02f,0.03f,0.04f}, 0.6f);
        if (py < hill) {
            c = mix(sky_at_horizon, ground, 0.4f);
        } else {
            float depth = (py - hill) / (s.pixel_h - hill + 1.f);
            c = scale(ground, 1.f - depth * 0.5f);
        }
    }
    return c;
}

static void paint(Canvas& cv, int W, int H) {
    using namespace stylecache;
    if (W < 20 || H < 8) return;

    std::time_t now = sim_now();
    std::tm lt{}; localtime_r(&now, &lt);

    SunPos sp = chronos::astro::sun_position(now, g.lat, g.lon);
    float sun_alt = (float)sp.altitude;
    auto moon = chronos::astro::moon_phase(now);

    int pixel_h = H * 2;
    int pixel_w = W;

    // Sun/moon screen position. Map azimuth (90=E left, 270=W right) to x and
    // altitude to a parabolic arc height.
    auto body_screen = [&](double az, double alt) {
        // Treat the visible sky as facing south: az 90(E)→left, 270(W)→right.
        float ax = (float)((az - 90.0) / 180.0);  // 0 at E .. 1 at W
        ax = std::clamp(ax, -0.2f, 1.2f);
        float bx = ax * pixel_w;
        // altitude 0..90 → screen y near horizon..top
        float horizon = pixel_h * 0.62f;
        float topy = pixel_h * 0.08f;
        float ay = std::clamp((float)alt / 60.f, -0.3f, 1.f);
        float by = horizon - ay * (horizon - topy);
        return std::pair<float,float>{bx, by};
    };
    auto [sun_x, sun_y]   = body_screen(sp.azimuth, sp.altitude);
    auto moon_pos_az = sp.azimuth + 180.0;          // moon roughly opposite-ish
    auto [moon_x, moon_y] = body_screen(std::fmod(moon_pos_az, 360.0),
                                        45.0 * (1.0 - std::abs(moon.frac - 0.5) * 2.0) + 10.0);

    float horizon_y = pixel_h * 0.62f;
    bool is_night = sun_alt < -6.f;
    float star_vis = smoothstep(0.f, -12.f, sun_alt);   // 0 day → 1 deep night

    SkyCtx sc{ sun_x, sun_y, moon_x, moon_y, sun_alt, horizon_y, star_vis,
               g.anim, (float)moon.frac, pixel_w, pixel_h, is_night };

    // ── render every pixel row (two pixels per terminal cell) ──────────────
    // Single-threaded by design: StylePool::intern() is not safe for
    // concurrent writers, and maya's SIMD cell-diff only emits the cells
    // that actually changed, so a mostly-static gradient is cheap anyway.
    for (int cy = 0; cy < H; ++cy) {
        for (int cx = 0; cx < W; ++cx) {
            float px = cx + 0.5f;
            Col top = shade_pixel(sc, px, cy * 2 + 0.0f);
            Col bot = shade_pixel(sc, px, cy * 2 + 1.0f);
            cv.set(cx, cy, U'\u2580', cell(top, bot));
        }
    }

    // ── HUD overlay ────────────────────────────────────────────────────────
    char buf[256];
    std::string clock = std::format("{:02}:{:02}", lt.tm_hour, lt.tm_min);
    Col txt = is_night ? Col{0.85f,0.88f,1.0f} : Col{0.08f,0.10f,0.16f};
    Col accent = is_night ? Col{0.7f,0.8f,1.0f} : Col{1.0f,0.97f,0.9f};

    int hud_x = 2, hud_y = 1;
    if (g.big_clock) {
        // render the seven-segment clock, each glyph 5 wide × 5 tall
        int gx = hud_x;
        for (char ch : clock) {
            int gi = bigfont::idx(ch);
            if (gi < 0) { gx += 2; continue; }
            for (int r = 0; r < 5; ++r)
                draw_text_over(cv, gx, hud_y + r, bigfont::G[gi][r], accent, true, sun_alt, pixel_h);
            gx += (ch == ':') ? 3 : 6;
        }
        std::snprintf(buf, sizeof(buf), ":%02d", lt.tm_sec);
        draw_text_over(cv, gx, hud_y + 4, buf, txt, false, sun_alt, pixel_h);
        hud_y += 6;
    } else {
        std::string big = std::format("{:02}:{:02}:{:02}", lt.tm_hour, lt.tm_min, lt.tm_sec);
        draw_text_over(cv, hud_x, hud_y, big, accent, true, sun_alt, pixel_h);
        hud_y += 2;
    }

    std::snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s %d, %d",
        WD_FULL[lt.tm_wday % 7], MON_FULL[lt.tm_mon + 1], lt.tm_mday, lt.tm_year + 1900);
    draw_text_over(cv, hud_x, hud_y, buf, txt, true, sun_alt, pixel_h);

    // top-right: place + phase label
    std::string phase = std::string(moon.name);
    std::snprintf(buf, sizeof(buf), "\xE2\x9F\xA1 %s", g.place.c_str());
    int rx = W - (int)std::string(buf).size() - 2;
    draw_text_over(cv, std::max(0, rx), 1, buf, accent, true, sun_alt, pixel_h);

    // bottom-left stat block: sunrise/sunset/daylight, moon illum
    double tz = chronos::timeutil::local_utc_offset_hours(now);
    auto st = chronos::astro::sun_times(lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday, g.lat, g.lon, tz);
    auto hm = [](double h){ int hh=(int)h%24; int mm=(int)std::round((h-std::floor(h))*60);
        if(mm==60){mm=0;hh=(hh+1)%24;} return std::format("{:02}:{:02}",hh,mm); };
    int sy = H - 6;
    if (st.valid && !st.always_up && !st.always_down) {
        int dh=(int)st.daylight_h, dm=(int)std::round((st.daylight_h-dh)*60);
        std::snprintf(buf,sizeof(buf),"\xE2\x86\x91 %s  \xE2\x86\x93 %s  \xE2\x98\x80 %dh%02dm",
            hm(st.sunrise_h).c_str(), hm(st.sunset_h).c_str(), dh, dm);
        draw_text_over(cv, hud_x, sy, buf, txt, false, sun_alt, pixel_h);
    }
    std::snprintf(buf,sizeof(buf),"%s %s  %.0f%% lit  \xE2\x98\x89 alt %+.0f\xC2\xB0",
        moon.glyph.c_str(), phase.c_str(), moon.illum*100, sun_alt);
    draw_text_over(cv, hud_x, sy+1, buf, txt, false, sun_alt, pixel_h);

    // status line
    std::string warp = g.warp_rate != 0
        ? std::format("warp x{:.0f}", g.warp_rate)
        : (g.time_warp != 0 ? std::format("+{:.0f}h", g.time_warp/3600.0) : std::string("live"));
    std::snprintf(buf,sizeof(buf),
        " c calendar \xC2\xB7 w clocks \xC2\xB7 a clock \xC2\xB7 +/- warp \xC2\xB7 0 now \xC2\xB7 q quit   [%s]",
        warp.c_str());
    Col barfg{0.95f,0.96f,1.0f};
    int by = H - 1;
    for (int x = 0; x < W; ++x) cv.set(x, by, U' ', fg_on(barfg, Col{0.05f,0.05f,0.09f}));
    draw_bar_text(cv, 1, by, buf, barfg, Col{0.05f,0.05f,0.09f});

    // ── floating panels ────────────────────────────────────────────────────
    if (g.show_calendar) paint_calendar(cv, W, H, lt);
    if (g.show_clocks)   paint_clocks(cv, W, H, now);
}

// ════════════════════════════════════════════════════════════════════════
//  Frosted floating panels (calendar / world clocks)
// ════════════════════════════════════════════════════════════════════════
static void fill_panel(Canvas& cv, int x0, int y0, int w, int h, Col bg) {
    using namespace stylecache;
    uint16_t st = fg_on(Col{0.8f,0.85f,1.0f}, bg);
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            cv.set(x, y, U' ', st);
    // rounded-ish border
    uint16_t bst = fg_on(Col{0.5f,0.6f,0.85f}, bg);
    for (int x = x0; x < x0 + w; ++x) {
        cv.set(x, y0, U'\u2500', bst);
        cv.set(x, y0 + h - 1, U'\u2500', bst);
    }
    for (int y = y0; y < y0 + h; ++y) {
        cv.set(x0, y, U'\u2502', bst);
        cv.set(x0 + w - 1, y, U'\u2502', bst);
    }
    cv.set(x0, y0, U'\u256d', bst);          cv.set(x0 + w - 1, y0, U'\u256e', bst);
    cv.set(x0, y0 + h - 1, U'\u2570', bst);  cv.set(x0 + w - 1, y0 + h - 1, U'\u256f', bst);
}

static void paint_calendar(Canvas& cv, int W, int H, const std::tm& today) {
    using namespace stylecache;
    int y = g.cal_year ? g.cal_year : today.tm_year + 1900;
    int mo = g.cal_month ? g.cal_month : today.tm_mon + 1;
    bool leap = (y%4==0 && y%100!=0) || (y%400==0);
    int dim_tbl[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    int dim = (mo==2 && leap) ? 29 : dim_tbl[mo];
    static const int t[]={0,3,2,5,0,3,5,1,4,6,2,4};
    int yy=y; if(mo<3) yy-=1;
    int fdow=((yy+yy/4-yy/100+yy/400+t[mo-1]+1)%7+6)%7;

    int pw = 24, ph = 11, px = (W - pw) / 2, py = (H - ph) / 2;
    Col bg{0.06f,0.07f,0.13f};
    fill_panel(cv, px, py, pw, ph, bg);
    char buf[64];
    std::snprintf(buf,sizeof(buf),"%s %d", MON_FULL[mo], y);
    int titlex = px + (pw - (int)std::string(buf).size())/2;
    draw_bar_text(cv, titlex, py+1, buf, Col{1.0f,0.85f,0.4f}, bg);
    draw_bar_text(cv, px+2, py+2, "Mo Tu We Th Fr Sa Su", Col{0.45f,0.5f,0.7f}, bg);

    int day=1, row=3;
    while (day<=dim) {
        for (int col=0; col<7; ++col) {
            int cellx = px+2+col*3;
            if ((day==1 && col<fdow) || day>dim) continue;
            bool todayp = (y==today.tm_year+1900 && mo==today.tm_mon+1 && day==today.tm_mday);
            bool weekend = col>=5;
            std::snprintf(buf,sizeof(buf),"%2d", day);
            Col fg = todayp ? Col{0.05f,0.06f,0.1f}
                   : weekend ? Col{0.95f,0.5f,0.6f} : Col{0.8f,0.85f,1.0f};
            Col cbg = todayp ? Col{0.5f,0.7f,1.0f} : bg;
            uint16_t st = fg_on(fg, cbg, todayp);
            std::string s(buf);
            cv.set(cellx,   py+row, s[0], st);
            cv.set(cellx+1, py+row, s[1], st);
            day++;
        }
        row++;
    }
    draw_bar_text(cv, px+2, py+ph-2, "h/l \xE2\x86\x90\xE2\x86\x92  c close", Col{0.4f,0.45f,0.65f}, bg);
}

static void paint_clocks(Canvas& cv, int W, int H, std::time_t now) {
    using namespace stylecache;
    auto zones = chronos::timeutil::default_zones();
    int rows = (int)zones.size() + 3;
    int pw = 30, ph = rows + 2, px = (W - pw) / 2, py = (H - ph) / 2;
    Col bg{0.06f,0.07f,0.13f};
    fill_panel(cv, px, py, pw, ph, bg);
    draw_bar_text(cv, px+2, py+1, "WORLD CLOCKS", Col{0.5f,0.8f,1.0f}, bg);

    std::tm lt{}; localtime_r(&now,&lt);
    char buf[80];
    std::snprintf(buf,sizeof(buf),"%-5s %s %02d:%02d  %s %d", "Local",
        (lt.tm_hour>=6&&lt.tm_hour<19)?"\xE2\x98\x80":"\xE2\x98\xBD",
        lt.tm_hour, lt.tm_min, WD3[lt.tm_wday%7], lt.tm_mday);
    draw_bar_text(cv, px+2, py+3, buf, Col{0.45f,0.9f,0.8f}, bg);
    int r=4;
    for (auto& z : zones) {
        auto rd = chronos::timeutil::read_zone(now, z);
        std::snprintf(buf,sizeof(buf),"%-5s %s %02d:%02d  %s %d", rd.label.c_str(),
            rd.is_day?"\xE2\x98\x80":"\xE2\x98\xBD", rd.hour, rd.minute,
            rd.weekday.c_str(), rd.day);
        draw_bar_text(cv, px+2, py+r, buf, Col{0.75f,0.85f,1.0f}, bg);
        r++;
    }
    draw_bar_text(cv, px+2, py+ph-1, "w close", Col{0.4f,0.45f,0.65f}, bg);
}

// ════════════════════════════════════════════════════════════════════════
//  Main
// ════════════════════════════════════════════════════════════════════════
static void init_state() {
    if (const char* la = std::getenv("CHRONOS_LAT")) g.lat = std::atof(la);
    if (const char* lo = std::getenv("CHRONOS_LON")) g.lon = std::atof(lo);
    if (const char* pl = std::getenv("CHRONOS_PLACE")) g.place = pl;
}

static void cal_step(int delta) {
    std::time_t now = sim_now(); std::tm lt{}; localtime_r(&now,&lt);
    if (!g.cal_year)  g.cal_year = lt.tm_year + 1900;
    if (!g.cal_month) g.cal_month = lt.tm_mon + 1;
    g.cal_month += delta;
    while (g.cal_month < 1)  { g.cal_month += 12; g.cal_year--; }
    while (g.cal_month > 12) { g.cal_month -= 12; g.cal_year++; }
}

int main() {
    init_state();
    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();

    (void)canvas_run(
        CanvasConfig{.fps = 30, .mouse = false, .mode = Mode::Fullscreen,
                     .auto_clear = false, .title = "chronos"},

        // on_resize — capture the style pool
        [&](StylePool& pool, int, int) { stylecache::g_pool = &pool; },

        // on_event
        [&](const Event& ev) -> bool {
            if (key(ev, 'q') || key(ev, SpecialKey::Escape)) return false;
            if (key(ev, 'a')) g.big_clock = !g.big_clock;
            if (key(ev, 'c')) { g.show_calendar = !g.show_calendar; g.show_clocks = false; }
            if (key(ev, 'w')) { g.show_clocks = !g.show_clocks; g.show_calendar = false; }
            if (key(ev, '0')) { g.time_warp = 0; g.warp_rate = 0; }
            if (key(ev, '+') || key(ev, '=')) g.warp_rate = g.warp_rate <= 0 ? 600 : g.warp_rate * 2;
            if (key(ev, '-') || key(ev, '_')) g.warp_rate = g.warp_rate >= 0 ? -600 : g.warp_rate * 2;
            if (g.show_calendar) {
                if (key(ev,'l')||key(ev,SpecialKey::Right)) cal_step(+1);
                if (key(ev,'h')||key(ev,SpecialKey::Left))  cal_step(-1);
            }
            return true;
        },

        // on_paint
        [&](Canvas& cv, int W, int H) {
            auto nowc = Clock::now();
            float dt = std::chrono::duration<float>(nowc - last).count();
            last = nowc;
            dt = std::min(dt, 0.1f);
            g.anim += dt;
            g.time_warp += g.warp_rate * dt;
            paint(cv, W, H);
        }
    );
    return 0;
}
