#pragma once
// chronos::ui::SkyWidget — the living sky background.
//
// Paints a per-pixel gradient driven by the sun's true altitude, plus a sun
// disc with glow, a phase-shaded moon, twinkling stars, drifting fBm clouds,
// and a rolling-hill horizon silhouette. This is the canvas everything else
// floats over.

#include "../widget.hpp"
#include <climits>
#include <cmath>
#include <vector>

namespace chronos::ui {

// ── live sky sample ───────────────────────────────────────────────────
// SkyWidget publishes its posterized cell cache here every paint, so widgets
// drawn OVER the sky (clock digits, date scrim) can composite against the REAL
// animated backdrop — clouds, glow, stars — instead of the flat palette colour.
// Without this, anything that bakes a backdrop goes stale the moment a cloud
// drifts behind it (visible as mismatched boxes around the glyphs).
inline const std::vector<gfx::Col>* g_sky_cells = nullptr;
inline int g_sky_w = 0, g_sky_h = 0, g_sky_ox = 0, g_sky_oy = 0;

// true + the cell's top/bottom sub-pixel colours when (cx,cy) is inside the
// most recent sky paint; false when no sky has been painted (fallback needed).
inline bool sky_live_cell(int cx, int cy, gfx::Col& top, gfx::Col& bot) {
    if (!g_sky_cells) return false;
    int lx = cx - g_sky_ox, ly = cy - g_sky_oy;
    if (lx < 0 || ly < 0 || lx >= g_sky_w || ly >= g_sky_h) return false;
    size_t i = (size_t(ly) * g_sky_w + lx) * 2;
    if (i + 1 >= g_sky_cells->size()) return false;
    top = (*g_sky_cells)[i];
    bot = (*g_sky_cells)[i + 1];
    return true;
}

// Keyframed sky palette by sun altitude (deg). Exposed so other widgets can
// sample the sky colour behind their text for legible scrims.
struct SkyBand { float alt; Col zenith; Col horizon; };
inline const SkyBand kSkyBands[] = {
    {-18.f, {0.01f,0.01f,0.04f}, {0.02f,0.02f,0.07f}},  // astronomical night
    { -8.f, {0.03f,0.04f,0.12f}, {0.10f,0.07f,0.18f}},  // deep twilight
    { -3.f, {0.07f,0.09f,0.22f}, {0.55f,0.28f,0.22f}},  // civil / golden
    {  0.f, {0.16f,0.22f,0.40f}, {0.95f,0.55f,0.30f}},  // sunrise / sunset
    {  6.f, {0.22f,0.40f,0.70f}, {0.85f,0.70f,0.62f}},  // early morning
    { 25.f, {0.18f,0.42f,0.82f}, {0.62f,0.78f,0.95f}},  // mid-day blue
    { 60.f, {0.13f,0.40f,0.88f}, {0.55f,0.78f,0.98f}},  // high noon
};

inline Col sky_palette(float alt, float v /*0 horizon..1 zenith*/) {
    constexpr int n = int(sizeof(kSkyBands) / sizeof(kSkyBands[0]));
    int i = 0;
    while (i < n - 1 && alt > kSkyBands[i + 1].alt) ++i;
    float t = (i < n - 1) ? gfx::smoothstep(kSkyBands[i].alt, kSkyBands[i + 1].alt, alt) : 0.f;
    Col zen = gfx::mix(kSkyBands[i].zenith,  kSkyBands[std::min(i+1,n-1)].zenith,  t);
    Col hor = gfx::mix(kSkyBands[i].horizon, kSkyBands[std::min(i+1,n-1)].horizon, t);
    return gfx::mix(hor, zen, std::pow(v, 0.65f));
}

// ── weather → scene parameters ───────────────────────────────────────────────
// Translates the live WMO weather code (+ wind) into a small set of dials the
// sky shader reads: how much cloud cover, how hard it's raining/snowing, fog
// density, storm darkening, and lightning. Everything is a smooth 0..1 so the
// scene can be cross-faded between conditions instead of snapping.
struct WeatherViz {
    float cloud   = 0.f;   // extra cloud cover (0 clear .. 1 full overcast)
    float rain    = 0.f;   // rain intensity 0..1
    float snow    = 0.f;   // snow intensity 0..1
    float fog     = 0.f;   // fog veil 0..1
    float storm   = 0.f;   // storm darkening 0..1 (also enables lightning)
    float wind    = 1.f;   // wind multiplier on cloud drift (1 = calm baseline)
    bool  active  = false; // we have real data driving the scene
};

inline WeatherViz weather_viz(const chronos::weather::Weather& w) {
    WeatherViz s;
    if (!w.valid) return s;          // no data → leave the fair-weather default
    s.active = true;
    s.wind = std::clamp(0.6f + (float)w.wind_kmh / 30.f, 0.6f, 3.0f);
    switch (w.code) {
        case 0:                 s.cloud = 0.00f; break;               // clear
        case 1:                 s.cloud = 0.18f; break;               // mainly clear
        case 2:                 s.cloud = 0.50f; break;               // partly cloudy
        case 3:                 s.cloud = 0.95f; break;               // overcast
        case 45: case 48:       s.cloud = 0.70f; s.fog = 0.85f; break; // fog
        case 51: case 53: case 55:                                    // drizzle
        case 56: case 57:       s.cloud = 0.80f; s.rain = 0.35f; break;
        case 61:                s.cloud = 0.85f; s.rain = 0.45f; break; // light rain
        case 63:                s.cloud = 0.90f; s.rain = 0.65f; break; // rain
        case 65:                s.cloud = 1.00f; s.rain = 0.95f; break; // heavy rain
        case 66: case 67:       s.cloud = 0.95f; s.rain = 0.70f; break; // freezing rain
        case 80:                s.cloud = 0.80f; s.rain = 0.55f; break; // showers
        case 81:                s.cloud = 0.90f; s.rain = 0.75f; break;
        case 82:                s.cloud = 1.00f; s.rain = 1.00f; break; // violent showers
        case 71:                s.cloud = 0.85f; s.snow = 0.45f; break; // light snow
        case 73:                s.cloud = 0.90f; s.snow = 0.70f; break;
        case 75:                s.cloud = 1.00f; s.snow = 1.00f; break; // heavy snow
        case 77:                s.cloud = 0.80f; s.snow = 0.40f; break; // snow grains
        case 85:                s.cloud = 0.90f; s.snow = 0.65f; break; // snow showers
        case 86:                s.cloud = 1.00f; s.snow = 0.95f; break;
        case 95:                s.cloud = 1.00f; s.rain = 0.70f; s.storm = 0.85f; break; // thunderstorm
        case 96: case 99:       s.cloud = 1.00f; s.rain = 0.85f; s.storm = 1.00f; break; // storm + hail
        default:                s.cloud = 0.40f; break;
    }
    return s;
}

class SkyWidget : public Widget {
public:
    const char* name() const override { return "sky"; }

    // Force a full reshade on the next paint. Called after any discrete input
    // (warp change, jump-to-now, overlay toggle) so the result appears at once
    // instead of trickling in over the amortized row-sweep.
    void invalidate() { dirty_ = true; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const int PW = r.w, PH = r.h * 2;
        const float horizon_y = PH * 0.62f;
        const float sun_alt = (float)c.sun.altitude;
        const bool  night = sun_alt < -6.f;

        // ── weather: ease the live conditions toward their target so a change
        //    (clear → storm) cross-fades over a couple of seconds instead of
        //    snapping. The eased dials drive cloud cover, storm darkening, and
        //    the precip/fog overlays below.
        WeatherViz tgt = weather_viz(c.weather);
        float ease = std::clamp((c.anim - last_anim_) * 0.6f, 0.f, 1.f);
        last_anim_ = c.anim;
        auto approach = [&](float& cur, float to){ cur += (to - cur) * ease; };
        approach(viz_.cloud, tgt.cloud); approach(viz_.rain, tgt.rain);
        approach(viz_.snow,  tgt.snow);  approach(viz_.fog,  tgt.fog);
        approach(viz_.storm, tgt.storm); approach(viz_.wind, tgt.wind);
        viz_.active = tgt.active;
        const WeatherViz vz = viz_;
        // a sky reshade whenever the eased weather has visibly moved, so the
        // cached scene tracks the new cloud cover / storm darkening promptly.
        if (std::abs(vz.cloud - shaded_cloud_) > 0.02f ||
            std::abs(vz.storm - shaded_storm_) > 0.02f) {
            dirty_ = true; shaded_cloud_ = vz.cloud; shaded_storm_ = vz.storm;
        }
        // During time-warp the sun altitude marches fast, so the amortized
        // row-sweep (which reshades only a slice of rows per frame) would leave
        // the grid a patchwork of rows shaded at DIFFERENT sun altitudes — a
        // visible horizontal tear/banding that drifts down the screen. When the
        // altitude moves more than a hair between frames (warp, or jump-to-now),
        // force a coherent full reshade so the whole sky updates in one frame.
        if (c.warp_rate != 0.0 || std::abs(sun_alt - shaded_sunalt_) > 0.15f) {
            dirty_ = true;
        }
        shaded_sunalt_ = sun_alt;

        // Scenery animation clock. The cached shader (clouds, water ripple,
        // glints, stars, aurora, meteors) animates off this. During warp the
        // sky reshades the WHOLE grid every frame (above) to keep the gradient
        // coherent — but if the scenery clock kept advancing, every cloud/water
        // cell would also change every frame, flooding the wire with thousands
        // of cell updates that a slow/mobile terminal drops (visible as torn
        // white streaks across the water + ground). So FREEZE the scenery clock
        // while warping: only the sun-driven gradient moves, keeping the
        // per-frame cell delta small enough for any terminal to apply cleanly.
        float anim = c.anim;
        if (c.warp_rate != 0.0) anim = frozen_anim_;
        else                    frozen_anim_ = c.anim;

        // lightning: during a storm, fire brief bright flashes at random. A
        // flash is a fast attack + slow decay envelope keyed off epochs of the
        // free-running clock. `flash` is a global 0..~1 brightness this frame.
        float flash = 0.f;
        if (vz.storm > 0.05f) {
            float T = anim / 2.6f;                   // ~one window every 2.6s
            float epoch = std::floor(T), prog = T - epoch;
            float seed = gfx::hash2(epoch * 5.3f, 1.9f);
            if (seed < 0.30f + 0.45f * vz.storm) {   // stormier = more frequent
                // double-strike: two quick decays inside the window
                float a = std::exp(-prog * 14.f);
                float b = 0.6f * std::exp(-std::abs(prog - 0.12f) * 22.f);
                flash = std::min(1.f, (a + b) * (0.5f + 0.5f * vz.storm));
            }
        }

        auto place = [&](double az, double alt) {
            float ax = std::clamp((float)((az - 90.0) / 180.0), -0.2f, 1.2f);
            float topy = PH * 0.08f;
            float ay = std::clamp((float)alt / 60.f, -0.3f, 1.f);
            return std::pair<float,float>{ ax * PW, horizon_y - ay * (horizon_y - topy) };
        };
        auto [sun_x, sun_y]   = place(c.sun.azimuth, c.sun.altitude);
        double maz = std::fmod(c.sun.azimuth + 180.0, 360.0);
        double malt = 45.0 * (1.0 - std::abs(c.moon.frac - 0.5) * 2.0) + 10.0;
        auto [moon_x, moon_y] = place(maz, malt);
        float moon_frac = (float)c.moon.frac;

        // aurora: only at high geographic latitude, on a clear-ish night. The
        // strength ramps with |lat| beyond ~48° and fades out under heavy cloud.
        float abslat = std::abs((float)c.lat);
        float aurora = gfx::smoothstep(48.f, 62.f, abslat) * (1.f - vz.cloud * 0.8f);

        // golden hour: a warm filmic grade that peaks when the sun sits low
        // (roughly the 40 min around sunrise/sunset) and fades out by midday or
        // full dark. `golden` 0..1 keys a whole-frame colour push toward warm
        // amber in the highlights + a teal lift in the shadows (classic
        // teal-and-orange film look). Killed under heavy storm cloud.
        float golden = gfx::smoothstep(10.f, 4.f, sun_alt)        // ramp up as sun drops
                     * gfx::smoothstep(-8.f, -1.f, sun_alt)       // off once truly set
                     * (1.f - vz.storm * 0.85f);

        // rainbow: appears when it's raining-but-clearing with the sun up and
        // fairly low. We arc it on the side of the sky OPPOSITE the sun, at the
        // ~42° antisolar radius. `rainbow` strength rises with how sunlit-yet-
        // still-wet the air is (rain present, not a black storm, sun low).
        float rainbow = std::clamp(vz.rain * 1.4f, 0.f, 1.f)
                      * gfx::smoothstep(0.f, 8.f, sun_alt)
                      * gfx::smoothstep(35.f, 16.f, sun_alt)
                      * (1.f - vz.storm) * (1.f - vz.cloud * 0.5f);

        // golden-hour film grade applied to a finished pixel: warm the
        // highlights toward amber, lift shadows toward teal, and add a touch of
        // overall warmth + contrast. Strength = `golden`. A no-op at golden==0.
        auto grade = [&](Col c0) -> Col {
            if (golden < 0.01f) return c0;
            float lum = 0.299f*c0.r + 0.587f*c0.g + 0.114f*c0.b;
            Col warm{1.06f, 0.92f, 0.70f};   // amber highlight push
            Col teal{0.78f, 0.95f, 1.04f};   // cool shadow lift
            // blend each pixel toward warm in the highlights, teal in shadows
            Col tint = gfx::mix(teal, warm, gfx::smoothstep(0.25f, 0.75f, lum));
            Col graded{ c0.r * tint.r, c0.g * tint.g, c0.b * tint.b };
            // a little global warmth + saturation for that low-sun glow
            graded = gfx::add(graded, gfx::scale(Col{0.10f,0.04f,0.f}, golden));
            return gfx::mix(c0, graded, golden);
        };

        // NO dither. Dither dissolves band edges into per-pixel speckle, which
        // on a crisp terminal reads as confetti static smeared over the whole
        // scene (user-rejected). Instead we posterize HARD — few, flat, clean
        // colour bands with razor edges: the Minecraft / pixel-art look. Bands
        // are a feature, not an artifact.
        //
        // ADAPTIVE pool guard kept: maya's StylePool caps at 65535 styles and
        // never evicts; at saturation the screen corrupts to default-styled
        // stripes. At 8 levels the palette is tiny so the thresholds below are
        // effectively unreachable — they remain as a hard backstop only.
        {
            size_t used = p.style_count();
            float want = levels_;
            if      (used > 52000) want = 5.f;
            else if (used > 40000) want = 6.f;
            if (want < levels_) { levels_ = want; dirty_ = true; }
        }
        const float SKY_LEVELS = levels_;          // sky + water bands
        const float GND_LEVELS = std::max(4.f, levels_ - 1.f);  // ground slightly coarser

        auto shade = [&](float px, float py) -> Col {
            float vv = 1.f - py / float(PH);
            if (py < horizon_y) {
                Col col = sky_palette(sun_alt, vv);

                // ── atmospheric scattering ─────────────────────────────────
                // A real sky is not a flat vertical gradient. Two cheap
                // approximations of Rayleigh/Mie scattering give it depth and
                // make low-sun skies genuinely glow:
                //   (a) Mie HORIZON BAND — a bright halo hugging the horizon
                //       (forward-scattered light) that reddens and intensifies
                //       as the sun drops, so sunrise/sunset set the whole
                //       horizon ablaze instead of a thin line at the sun.
                //   (b) AZIMUTHAL warmth — the dome is brighter and warmer on
                //       the sun's side, cooler opposite, so the sky has a real
                //       light direction rather than a symmetric wash.
                {
                    // proximity to the horizon (0 at zenith .. 1 on the line)
                    float hb = gfx::smoothstep(horizon_y * 0.55f, horizon_y, py);
                    // how low the sun is: peaks the band around sunrise/sunset
                    float low = gfx::smoothstep(16.f, -2.f, sun_alt)
                              * gfx::smoothstep(-12.f, -2.f, sun_alt);
                    // warm sunset band reddens as the sun sinks; cooler by day
                    Col band = gfx::mix(Col{1.00f,0.78f,0.50f},  // daytime cream-gold
                                        Col{1.00f,0.42f,0.24f},  // low-sun ember
                                        gfx::smoothstep(10.f, -1.f, sun_alt));
                    // brighter on the sun's azimuth side of the sky
                    float toward = gfx::smoothstep(PW * 0.85f, 0.f, std::abs(px - sun_x));
                    float mie = hb * hb * (0.16f + 0.55f * low) * (0.45f + 0.55f * toward);
                    col = gfx::add(col, gfx::scale(band, mie * (1.f - vz.cloud * 0.55f)));

                    // azimuthal directional tint across the whole dome
                    float az = (sun_x / PW - 0.5f);                // -0.5..0.5 sun side
                    float side = (px / PW - 0.5f) * (az > 0 ? 1.f : -1.f);
                    float warm_dir = std::clamp(side * 0.5f + 0.5f, 0.f, 1.f);
                    float daylight = gfx::smoothstep(-4.f, 10.f, sun_alt);
                    Col tint = gfx::mix(Col{0.0f,0.01f,0.04f},     // cool away-side
                                        Col{0.06f,0.04f,0.0f},     // warm sun-side
                                        warm_dir);
                    col = gfx::add(col, gfx::scale(tint,
                                   (0.4f + 0.6f * vv) * (0.3f + 0.7f * daylight)));
                }

                // sun: limb-darkened disc + warm corona + radial glow
                if (sun_alt > -2.f) {
                    float ds = std::hypot(px - sun_x, py - sun_y);
                    float R = 3.4f;
                    Col sc = gfx::mix(Col{1,0.95f,0.75f}, Col{1,0.72f,0.40f},
                                      gfx::smoothstep(20.f, 0.f, sun_alt));
                    // broad atmospheric glow
                    float glow = std::pow(gfx::smoothstep(PW * 0.55f, 0.f, ds), 1.6f) * 0.5f;
                    col = gfx::add(col, gfx::scale(sc, glow));
                    // tight corona just outside the disc
                    float corona = gfx::smoothstep(R * 3.5f, R, ds) * 0.5f;
                    col = gfx::add(col, gfx::scale(sc, corona));
                    // the disc with limb darkening toward the edge
                    float disc = gfx::smoothstep(R + 0.6f, R - 0.6f, ds);
                    float limb = 1.f - 0.35f * gfx::smoothstep(0.f, R, ds);
                    Col core = gfx::mix(Col{1,0.97f,0.86f}, Col{1.0f,0.93f,0.72f}, 1.f - limb);
                    col = gfx::mix(col, gfx::scale(core, limb + 0.2f), disc);

                    // crepuscular rays: angular shafts of light streaming out
                    // from the disc. The angle to the sun keys a slowly-rotating
                    // band of noise so the shafts shimmer and drift; they fade
                    // with distance and are strongest at low (golden) sun.
                    {
                        float ang = std::atan2(py - sun_y, px - sun_x);
                        float beams = gfx::fbm(ang * 2.4f + anim * 0.05f, ds * 0.012f);
                        float shaft = gfx::smoothstep(0.45f, 0.85f, beams);
                        float reach = std::pow(gfx::smoothstep(PW * 0.6f, R * 2.f, ds), 1.3f);
                        float low   = gfx::smoothstep(18.f, 0.f, sun_alt);  // golden bias
                        col = gfx::add(col, gfx::scale(sc,
                                       shaft * reach * (0.10f + 0.22f * low)));
                    }
                }
                // moon
                if (night || sun_alt < 6.f) {
                    float dxm = px - moon_x;
                    float dm = std::hypot(dxm, py - moon_y);
                    float mvis = gfx::smoothstep(0.f, -4.f, sun_alt);
                    float disc = gfx::smoothstep(2.6f, 1.6f, dm) * mvis;
                    float glow = gfx::smoothstep(10.f, 0.f, dm) * 0.18f * mvis;
                    Col mc{0.86f, 0.88f, 0.95f};
                    col = gfx::add(col, gfx::scale(mc, glow));
                    float lit = std::cos((moon_frac - 0.5f) * 2.f * 3.14159f);
                    float shadow = gfx::smoothstep(-lit, -lit + 0.6f, dxm / 2.0f);
                    col = gfx::mix(col, gfx::mix(gfx::scale(mc, 0.25f), mc, shadow), disc);
                }
                // stars: varied brightness/colour + a faint Milky-Way band.
                // Gated to REAL darkness: at dusk (sun -2..-6) the sky still has
                // sunset colour, and a faint star here gets snapped up to a full
                // band by the later posterize → ugly hard cream blocks. So ramp
                // star_vis from a deeper angle and require a solid floor before
                // drawing anything.
                float star_show = gfx::smoothstep(-7.f, -13.f, sun_alt);
                if (star_show > 0.05f && py < horizon_y * 0.96f) {
                    // ── aurora borealis: undulating green/magenta curtains in
                    //    the upper sky. Built from vertical "rays" whose base
                    //    height waves with low-freq noise + time, so the whole
                    //    sheet ripples and breathes. Colour shifts green at the
                    //    bottom to magenta at the crest. High latitudes only.
                    if (aurora > 0.01f) {
                        float topband = gfx::smoothstep(horizon_y * 0.92f, 0.f, py);
                        // two overlapping curtains drifting at different speeds
                        for (int L = 0; L < 2; ++L) {
                            float ph  = L * 17.3f;
                            float spd = (L ? 0.10f : 0.16f);
                            // wavy lower edge of the curtain (in pixels)
                            float wave = gfx::fbm(px * 0.018f + anim * spd + ph, 0.7f + ph);
                            float base = horizon_y * (0.34f + L * 0.10f) - wave * PH * 0.10f;
                            // vertical falloff above the wavy base = the sheet
                            float sheet = gfx::smoothstep(base, base - PH * 0.16f, py)
                                        * gfx::smoothstep(base - PH * 0.42f, base - PH * 0.16f, py);
                            // ray structure: bright vertical streaks across x
                            float rays = 0.55f + 0.45f * gfx::fbm(px * 0.10f + anim * spd * 1.7f + ph, 3.f);
                            float a = sheet * rays * topband * aurora * (L ? 0.7f : 1.0f);
                            // green low, cyan/magenta toward the crest
                            float h = gfx::smoothstep(base, base - PH * 0.30f, py);
                            Col ac = gfx::mix(Col{0.10f,0.95f,0.45f},  // green
                                              Col{0.55f,0.25f,0.95f},  // violet crest
                                              h * (0.5f + 0.5f * (float)L));
                            col = gfx::add(col, gfx::scale(ac, a * 0.5f * star_show));
                        }
                    }

                    // Milky-Way: a soft diagonal band of unresolved glow
                    float mwd = std::abs((py - horizon_y * 0.35f) - (px - PW * 0.5f) * 0.28f);
                    float mw = gfx::smoothstep(PH * 0.16f, 0.f, mwd);
                    float mwn = gfx::fbm(px * 0.06f, py * 0.12f + 5.f);
                    col = gfx::add(col, gfx::scale(Col{0.18f,0.20f,0.30f},
                                   mw * mwn * 0.5f * star_show));
                    // discrete stars on a fine grid, sub-cell positioned
                    float gx = std::floor(px), gy = std::floor(py);
                    float hs = gfx::hash2(gx * 1.7f, gy * 2.3f);
                    if (hs > 0.972f) {
                        float bright = (hs - 0.972f) / 0.028f;       // 0..1
                        float tw = 0.55f + 0.45f * std::sin(anim * 2.5f + hs * 60.f);
                        // colour by a second hash: warm vs cool stars
                        float hc = gfx::hash2(gy * 3.1f, gx * 1.3f);
                        Col scol = gfx::mix(Col{1.0f,0.85f,0.7f}, Col{0.75f,0.85f,1.0f}, hc);
                        // sub-pixel falloff for a clean point
                        float sxp = gfx::hash2(gx * 5.1f, gy * 7.3f);
                        float syp = gfx::hash2(gx * 7.7f, gy * 3.9f);
                        float dpt = std::hypot(px - (gx + sxp), py - (gy + syp));
                        float pt = gfx::smoothstep(0.9f + bright * 0.6f, 0.f, dpt);
                        col = gfx::add(col, gfx::scale(scol,
                                       pt * (0.4f + bright) * tw * star_show));
                    }

                    // ── constellations: a few named asterisms drawn as bright
                    //    anchor stars joined by faint connecting lines. Only on
                    //    clear-ish nights (fade out under cloud) so they don't
                    //    fight the overcast deck. Coords are normalised to the
                    //    upper sky box and slowly drift westward with the night.
                    float clear = (1.f - vz.cloud) * (1.f - vz.fog) * star_show;
                    if (clear > 0.10f) {
                        // Each constellation is a list of normalised (x,y) nodes
                        // in [0,1]×[0,1] over the star box, plus edges between
                        // consecutive index pairs.
                        struct Star2 { float x, y; };
                        struct Edge  { int a, b; };
                        // Big Dipper / Plough
                        static const Star2 dip[] = {
                            {0.06f,0.30f},{0.16f,0.24f},{0.26f,0.30f},{0.35f,0.38f},
                            {0.44f,0.33f},{0.52f,0.45f},{0.40f,0.50f}
                        };
                        static const Edge dipE[] = {{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,3}};
                        // Orion (belt + shoulders/feet)
                        static const Star2 ori[] = {
                            {0.66f,0.20f},{0.78f,0.22f},   // shoulders
                            {0.70f,0.40f},{0.73f,0.43f},{0.76f,0.46f}, // belt
                            {0.64f,0.62f},{0.80f,0.60f}    // feet
                        };
                        static const Edge oriE[] = {{0,2},{1,4},{2,3},{3,4},{2,5},{4,6},{0,1}};
                        // Cassiopeia (the W)
                        static const Star2 cas[] = {
                            {0.30f,0.66f},{0.38f,0.58f},{0.46f,0.66f},{0.54f,0.56f},{0.62f,0.66f}
                        };
                        static const Edge casE[] = {{0,1},{1,2},{2,3},{3,4}};

                        struct Grp { const Star2* s; int ns; const Edge* e; int ne; };
                        const Grp groups[] = {
                            { dip, 7, dipE, 7 },
                            { ori, 7, oriE, 7 },
                            { cas, 5, casE, 4 },
                        };
                        // map a normalised node into pixel space (upper sky box),
                        // drifting slowly west over the night.
                        float drift = std::fmod(anim * 0.004f, 1.f);
                        auto node_px = [&](const Star2& s) -> std::pair<float,float> {
                            float nx = std::fmod(s.x + 0.4f - drift + 1.f, 1.f);
                            float bx = 0.06f * PW + nx * 0.88f * PW;
                            float by = 0.06f * horizon_y + s.y * 0.62f * horizon_y;
                            return {bx, by};
                        };
                        float bestPt = 0.f, bestLine = 0.f;
                        for (const Grp& g : groups) {
                            // anchor stars: bright twinkling points
                            for (int i = 0; i < g.ns; ++i) {
                                auto [sx, sy] = node_px(g.s[i]);
                                float d = std::hypot(px - sx, py - sy);
                                float tw = 0.6f + 0.4f * std::sin(anim*2.f + i*1.7f + g.s[i].x*40.f);
                                bestPt = std::max(bestPt, gfx::smoothstep(1.5f, 0.f, d) * tw);
                            }
                            // connecting lines: distance to each segment
                            for (int k = 0; k < g.ne; ++k) {
                                auto [ax, ay] = node_px(g.s[g.e[k].a]);
                                auto [bx, by] = node_px(g.s[g.e[k].b]);
                                float vx = bx - ax, vy = by - ay;
                                float L2 = vx*vx + vy*vy + 1e-3f;
                                float t = std::clamp(((px-ax)*vx + (py-ay)*vy) / L2, 0.f, 1.f);
                                float cxp = ax + t*vx, cyp = ay + t*vy;
                                float dl = std::hypot(px - cxp, py - cyp);
                                bestLine = std::max(bestLine, gfx::smoothstep(0.7f, 0.f, dl));
                            }
                        }
                        Col cstar{0.80f,0.88f,1.0f};
                        col = gfx::add(col, gfx::scale(cstar, bestPt * 0.9f * clear));
                        col = gfx::add(col, gfx::scale(Col{0.30f,0.40f,0.60f},
                                       bestLine * 0.22f * clear));
                    }

                    // shooting stars: every ~6s a meteor crosses the sky. The
                    // epoch index seeds a random entry point + slope; within the
                    // epoch a 0..1 progress sweeps the head along the streak and
                    // a short bright tail trails behind it, fading out.
                    {
                        float T = anim / 6.0f;
                        float epoch = std::floor(T);
                        float prog  = T - epoch;                       // 0..1 in epoch
                        float seed  = gfx::hash2(epoch * 12.9f, 7.1f);
                        if (seed > 0.35f && prog < 0.5f) {             // ~65% of epochs fire
                            float life = prog / 0.5f;                  // 0..1 streak life
                            float sx0 = (0.1f + 0.8f * gfx::hash2(epoch, 3.7f)) * PW;
                            float sy0 = (0.05f + 0.35f * gfx::hash2(epoch, 9.2f)) * horizon_y;
                            float dir = (gfx::hash2(epoch, 1.1f) - 0.5f);
                            float hx  = sx0 + life * PW * 0.5f;        // head moves right-down
                            float hy  = sy0 + life * PW * (0.18f + dir * 0.2f);
                            // distance to the streak segment (head back along travel dir)
                            float tvx = PW * 0.5f, tvy = PW * (0.18f + dir * 0.2f);
                            float tl  = std::hypot(tvx, tvy) + 1e-3f;
                            float ux = tvx / tl, uy = tvy / tl;
                            float rx = px - hx, ry = py - hy;
                            float along = rx * ux + ry * uy;           // <=0 behind head
                            float perp  = std::abs(rx * uy - ry * ux);
                            float tail  = 9.0f;
                            // clamp the streak above the horizon (no meteors on land)
                            if (py < horizon_y * 0.96f &&
                                along <= 0.5f && along > -tail && perp < 1.4f) {
                                float head = gfx::smoothstep(2.0f, 0.f, std::hypot(rx, ry));
                                float body = gfx::smoothstep(tail, 0.f, -along) *
                                             gfx::smoothstep(1.4f, 0.f, perp);
                                float fade = (1.f - life) * star_show;
                                col = gfx::add(col, gfx::scale(
                                    Col{0.95f, 0.97f, 1.0f}, (head + body * 0.7f) * fade));
                            }
                        }
                    }
                }
                // clouds: domain-warped volumetric layers advected by wind.
                // Two parallax decks give depth: high thin cirrus streaking
                // fast, low fat cumulus rolling slow. Real cumulus is curdled,
                // not smooth fBm — so the sample coords are WARPED by a second
                // noise field (billowing cauliflower structure), and clouds
                // ADVECT horizontally (wind) rather than just shimmering in xy.
                {
                    float band = gfx::smoothstep(horizon_y, horizon_y * 0.20f, py);
                    float cirrus_band0 = gfx::smoothstep(horizon_y * 0.62f, 0.f, py);
                    // skip all cloud noise where neither deck can show (lower
                    // sky) — saves ~5 fBm calls/pixel over the bottom third. The
                    // storm/flash pass below still runs, so a stormy lower sky
                    // stays dark.
                    if (band >= 0.01f || cirrus_band0 >= 0.01f) {
                    float day  = gfx::smoothstep(-6.f, 8.f, sun_alt);
                    float wind = anim * 2.2f * vz.wind;     // drift scaled by real wind

                    // normalised cloud-space coords (decouple shape from term size)
                    float u = px * 0.030f, v = py * 0.075f;

                    // weather cloud cover lowers the density threshold so more of
                    // the sky fills in; full overcast pushes it to a near-solid
                    // grey deck. `cover` 0 (fair) .. 1 (overcast).
                    float cover = vz.cloud;
                    float cu_lo = 0.54f - cover * 0.34f;   // 0.54 fair .. 0.20 overcast
                    float cu_hi = 0.60f - cover * 0.30f;

                    // ── domain warp: offset the lookup by a low-freq flow field.
                    // This bends the noise into billows instead of round blobs.
                    float wx = gfx::fbm(u * 0.5f - wind * 0.026f, v * 0.5f + 3.1f);
                    float wy = gfx::fbm(u * 0.5f + 5.7f, v * 0.5f - wind * 0.020f);
                    float warp = 1.6f;

                    // ── low cumulus deck: fat, slow, billowing ────────────────
                    float cu = gfx::fbm(u + (wx - 0.5f) * warp - wind * 0.045f,
                                        v + (wy - 0.5f) * warp + 12.3f);
                    // sharpen tops: cauliflower bias makes crests bulge upward
                    cu = cu * 0.7f + gfx::fbm(u * 2.1f - wind * 0.060f,
                                              v * 2.1f + 7.0f) * 0.3f;
                    float cumulus = gfx::smoothstep(cu_lo, cu_hi, cu) * band;

                    // ── high cirrus deck: thin, fast, stretched horizontally ──
                    float ci = gfx::fbm(u * 0.6f - wind * 0.16f,
                                        v * 2.4f + 40.0f);   // y-stretched = streaky
                    float cirrus = gfx::smoothstep(0.62f - cover * 0.20f, 0.70f - cover * 0.18f, ci)
                                 * cirrus_band0 * 0.6f;

                    // composite (cumulus dominates where present)
                    float dens = std::max(cumulus, cirrus * 0.8f);
                    if (dens > 0.008f) {
                        // self-shadow: sample density a step toward the sun; if
                        // it's denser there, this pixel sits in the cloud's shade.
                        float sdx = (sun_x - px), sdy = (sun_y - py);
                        float sl = 1.f / (std::hypot(sdx, sdy) + 1e-3f);
                        float ahead = gfx::fbm(u + (wx - 0.5f) * warp - wind * 0.045f
                                                 + sdx * sl * 0.18f,
                                               v + (wy - 0.5f) * warp + 12.3f
                                                 + sdy * sl * 0.18f);
                        float lit = std::clamp(1.0f - (ahead - cu) * 2.4f, 0.22f, 1.f);
                        // ambient occlusion: cloud bases (high density core) darker
                        float ao = 1.f - 0.25f * gfx::smoothstep(0.6f, 0.95f, cumulus);
                        lit *= ao;

                        Col lo = gfx::mix(Col{0.05f,0.06f,0.12f},
                                          Col{0.34f,0.34f,0.42f}, day);
                        Col hi = gfx::mix(Col{0.55f,0.40f,0.42f},
                                          Col{1.0f,0.99f,0.98f},
                                          gfx::smoothstep(-6.f, 4.f, sun_alt));
                        // storm clouds turn heavy charcoal grey; overcast pulls
                        // the highlight toward a flat dull grey deck.
                        lo = gfx::mix(lo, Col{0.05f,0.05f,0.07f}, vz.storm * 0.8f);
                        hi = gfx::mix(hi, Col{0.40f,0.41f,0.46f}, vz.storm * 0.7f);
                        hi = gfx::mix(hi, Col{0.62f,0.63f,0.68f}, vz.cloud * 0.35f * day);
                        Col cc = gfx::mix(lo, hi, lit);

                        // warm/gold rim where the cloud edge catches a low sun
                        if (sun_alt > -4.f && sun_alt < 16.f) {
                            float edge = gfx::smoothstep(0.50f, 0.60f, cu) *
                                         (1.f - gfx::smoothstep(0.60f, 0.72f, cu));
                            // brightest on the side facing the sun
                            float facing = gfx::smoothstep(0.f, 1.f,
                                            0.5f + 0.5f * ((sun_x - px) / PW) * 2.f);
                            Col gold = gfx::mix(Col{1.0f,0.45f,0.22f},
                                                Col{1.0f,0.80f,0.45f},
                                                gfx::smoothstep(0.f,12.f,sun_alt));
                            cc = gfx::add(cc, gfx::scale(gold,
                                          edge * (0.35f + 0.45f * facing)));
                        }
                        // crisp cloud: edges snap to solid (no translucent
                        // fade), so clouds read as defined white shapes against
                        // the flat sky — a thin 1-step soft fringe avoids jaggies.
                        float opacity = gfx::smoothstep(0.04f, 0.30f, dens);
                        col = gfx::mix(col, cc, opacity);
                    }
                    } // end cloud-body guard
                }
                // ── rainbow: a 42° primary arc (+ faint secondary) centred on
                //    the antisolar point. We measure each pixel's angular radius
                //    from that point; bands of spectral colour land in a thin
                //    annulus. Drawn over the sky/cloud composite so it reads as
                //    light in the air. Only when `rainbow` strength is non-trivial.
                if (rainbow > 0.02f && py < horizon_y) {
                    // antisolar point = mirror of the sun through screen centre,
                    // dropped below the horizon-ish so the arc bows upward.
                    float ax = 2.f * (PW * 0.5f) - sun_x;
                    float ay = horizon_y + (horizon_y - sun_y) * 0.6f;
                    float rr = std::hypot(px - ax, py - ay);
                    // radius of the primary band, in pixels, scaled to the frame
                    float R0 = PW * 0.46f;
                    float bandw = PW * 0.060f;            // arc thickness
                    // primary arc: red outside → violet inside across the band
                    float t = (rr - (R0 - bandw)) / bandw;   // 0 inner .. 1 outer
                    float in_band = gfx::smoothstep(-0.05f, 0.08f, t)
                                  * gfx::smoothstep(1.05f, 0.92f, t);
                    if (in_band > 0.001f && py < ay) {       // upper arc only
                        // spectral ramp red(outer)→violet(inner)
                        Col spec;
                        float h = std::clamp(t, 0.f, 1.f);   // 0 violet .. 1 red
                        if      (h < 0.25f) spec = gfx::mix(Col{0.45f,0.20f,0.85f}, Col{0.20f,0.45f,0.95f}, h/0.25f);
                        else if (h < 0.50f) spec = gfx::mix(Col{0.20f,0.45f,0.95f}, Col{0.25f,0.85f,0.45f}, (h-0.25f)/0.25f);
                        else if (h < 0.75f) spec = gfx::mix(Col{0.25f,0.85f,0.45f}, Col{0.98f,0.85f,0.25f}, (h-0.50f)/0.25f);
                        else                spec = gfx::mix(Col{0.98f,0.85f,0.25f}, Col{0.95f,0.35f,0.25f}, (h-0.75f)/0.25f);
                        col = gfx::add(col, gfx::scale(spec, in_band * rainbow * 0.42f));
                    }
                    // faint secondary arc, wider radius, reversed order, dimmer
                    float R1 = R0 + bandw * 2.6f;
                    float t2 = (rr - (R1 - bandw)) / bandw;
                    float in2 = gfx::smoothstep(-0.05f, 0.08f, t2)
                              * gfx::smoothstep(1.05f, 0.92f, t2);
                    if (in2 > 0.001f && py < ay) {
                        float h = std::clamp(1.f - t2, 0.f, 1.f);   // reversed
                        Col spec = gfx::mix(Col{0.45f,0.25f,0.80f}, Col{0.90f,0.45f,0.30f}, h);
                        col = gfx::add(col, gfx::scale(spec, in2 * rainbow * 0.16f));
                    }
                }
                // storm: drain colour and darken the whole sky so the scene
                // reads as a brooding overcast; a lightning flash briefly lifts
                // it toward a cold blue-white.
                if (vz.storm > 0.01f) {
                    Col gloom = gfx::scale(col, 0.55f);
                    gloom = gfx::mix(gloom, Col{0.10f,0.11f,0.14f}, 0.35f);
                    col = gfx::mix(col, gloom, vz.storm);
                }
                if (flash > 0.001f)
                    col = gfx::add(col, gfx::scale(Col{0.55f,0.62f,0.85f}, flash * 0.9f));
                return gfx::posterize(gfx::saturate(grade(col), 1.25f), SKY_LEVELS);
            }
            // ground — layered rolling hills with atmospheric depth.
            // Sky colour at the horizon, used to tint distant ridges (haze).
            Col horizon_col = sky_palette(sun_alt, 0.f);
            float day = gfx::smoothstep(-8.f, 6.f, sun_alt);   // 0 night .. 1 day
            float sun_dir = std::clamp((sun_x / PW - 0.5f) * 2.f, -1.f, 1.f);

            // ── reflective lake ────────────────────────────────────────────
            // The lowest slice of the scene is still water that mirrors the
            // sky: the surface sits at `shore_y`, and a pixel below it samples
            // the sky colour reflected across that line, with a horizontal
            // ripple wobble + a wavering specular glint from the sun/moon. This
            // is computed from the palette + light discs (not the full cloud
            // shader) so it stays cheap while reading convincingly like a lake.
            float shore_y = horizon_y + PH * 0.30f;
            if (py >= shore_y) {
                // depth 0 at the shoreline .. 1 at the very bottom
                float depth = std::clamp((py - shore_y) / std::max(1.f, float(PH) - shore_y), 0.f, 1.f);
                // ripple: a travelling wave perturbs which sky row we mirror,
                // stronger toward the viewer (foreground) so near water is choppier.
                float ripple = std::sin(py * 0.55f - anim * 1.6f) * (0.6f + 2.2f * depth)
                             + gfx::fbm(px * 0.10f + anim * 0.3f, py * 0.20f) * 1.5f * depth;
                float mir_y = std::clamp(2.f * shore_y - py + ripple, 0.f, shore_y - 1.f);
                float mv = 1.f - mir_y / float(PH);
                Col water = sky_palette(sun_alt, mv);

                // reflected sun glint: a wavering bright column under the sun
                if (sun_alt > -3.f) {
                    float gx = std::abs(px - sun_x + std::sin(py * 0.8f - anim * 2.f) * 2.5f * depth);
                    float glint = gfx::smoothstep(PW * 0.10f, 0.f, gx)
                                * (0.35f + 0.65f * std::abs(std::sin(py * 0.9f - anim * 3.f)));
                    Col sc = gfx::mix(Col{1.f,0.7f,0.4f}, Col{1.f,0.92f,0.7f},
                                      gfx::smoothstep(0.f, 16.f, sun_alt));
                    water = gfx::add(water, gfx::scale(sc, glint * (0.4f + 0.4f*day)));
                }
                // reflected moon glint at night
                if (sun_alt < 4.f) {
                    float gx = std::abs(px - moon_x + std::sin(py * 0.7f - anim * 1.6f) * 2.0f * depth);
                    float mvis = gfx::smoothstep(2.f, -6.f, sun_alt);
                    float glint = gfx::smoothstep(PW * 0.06f, 0.f, gx)
                                * (0.3f + 0.7f * std::abs(std::sin(py * 0.8f - anim * 2.2f)));
                    water = gfx::add(water, gfx::scale(Col{0.80f,0.85f,0.98f}, glint * 0.5f * mvis));
                }
                // tint deeper water toward a cool teal + darken with depth so
                // the surface reads as a real body, not just a flipped sky.
                Col deep = gfx::mix(Col{0.04f,0.10f,0.16f}, Col{0.02f,0.05f,0.10f}, day < 0.3f ? 1.f : 0.f);
                water = gfx::mix(water, deep, 0.30f + 0.45f * depth);
                // a crisp specular highlight band right at the shoreline edge
                float edge = gfx::smoothstep(2.5f, 0.f, py - shore_y);
                water = gfx::add(water, gfx::scale(horizon_col, edge * 0.25f));
                // storm/flash also touch the water so it stays consistent
                if (vz.storm > 0.01f) water = gfx::mix(water, gfx::scale(water,0.5f), vz.storm);
                if (flash > 0.001f)   water = gfx::add(water, gfx::scale(Col{0.5f,0.57f,0.8f}, flash*0.7f));
                return gfx::posterize(gfx::saturate(grade(water), 1.25f), SKY_LEVELS);
            }

            // Three ridges: far (hazy, high) → near (saturated, low). Day greens
            // are vivid sunlit grass; night/twilight keep them deep. `depth` 0..1
            // (far→near) drives saturation + how much haze washes the ridge.
            struct Ridge { float base; float amp; float freq; float phase;
                           Col lo; Col hi; float depth; };
            const Ridge ridges[] = {
                // far rolling hills, hazed toward the horizon sky
                { horizon_y + PH * 0.020f, 2.5f, 0.018f, 0.4f,
                  {0.16f,0.26f,0.22f}, {0.34f,0.50f,0.40f}, 0.0f },
                // mid green slope, lusher
                { horizon_y + PH * 0.075f, 4.0f, 0.030f, 2.1f,
                  {0.10f,0.22f,0.12f}, {0.26f,0.48f,0.22f}, 0.55f },
                // near foreground meadow: brightest, most saturated grass
                { horizon_y + PH * 0.170f, 6.0f, 0.045f, 4.7f,
                  {0.07f,0.18f,0.08f}, {0.24f,0.52f,0.18f}, 1.0f },
            };

            Col col = horizon_col;            // start from the sky we sit under
            bool drawn = false;
            for (const Ridge& rg : ridges) {
                float crest = rg.base
                            + rg.amp * std::sin(px * rg.freq + rg.phase)
                            + rg.amp * 0.5f * std::sin(px * rg.freq * 2.3f + rg.phase * 1.7f);
                if (py < crest) continue;     // above this ridge's silhouette
                drawn = true;
                // base vertical shade within the ridge band (lit crest → dark base)
                float lo_t = std::clamp((py - crest) / (PH * 0.12f), 0.f, 1.f);
                Col terr = gfx::mix(rg.hi, rg.lo, lo_t);

                // directional sun lighting: slopes facing the sun brighten, away
                // darken — gives the hills real form instead of flat fills.
                float slope = std::cos(px * rg.freq + rg.phase);   // -1..1 facing
                float facing = slope * sun_dir;                    // -1 away .. +1 toward
                terr = gfx::mix(terr, gfx::add(terr, Col{0.16f,0.18f,0.06f}),
                                std::max(0.f, facing) * day * 0.55f);   // sunlit warm-green
                terr = gfx::scale(terr, 1.f - std::max(0.f, -facing) * day * 0.30f); // shaded

                // ground texture: a multi-scale fbm breakup so large flats get
                // visible grain/dapple instead of one solid posterized block.
                float tex = gfx::fbm(px * 0.18f, (py - crest) * 0.22f + rg.phase) * 0.7f
                          + gfx::fbm(px * 0.55f, (py - crest) * 0.6f + rg.phase) * 0.3f;
                terr = gfx::scale(terr, 0.82f + 0.40f * tex);

                // atmospheric haze: only the DISTANT ridge washes toward the
                // sky; near hills stay crisp and saturated (big change — the old
                // code hazed every ridge during day = flat grey-green smear).
                float dist_haze = (1.f - rg.depth);                // 1 far .. 0 near
                float band_haze = gfx::smoothstep(horizon_y + PH * 0.10f, horizon_y, crest);
                float haze = dist_haze * band_haze * (0.30f + 0.30f * day);
                terr = gfx::mix(terr, horizon_col, haze);
                col = terr;
            }
            if (!drawn) {
                // The thin strip between the horizon line and the first ridge.
                // Make it a distinct distant-land band (haze-green) rather than
                // a flat wash of sky colour, so the horizon reads as a clean edge.
                float t = gfx::smoothstep(horizon_y, horizon_y + PH * 0.04f, py);
                Col far_land = gfx::mix(horizon_col, Col{0.30f, 0.42f, 0.40f}, 0.55f);
                col = gfx::mix(horizon_col, far_land, t);
            }
            // warm horizon spill: the lit sky bleeds onto the land near the sun
            if (sun_alt > -4.f) {
                float spill = gfx::smoothstep(PW * 0.55f, 0.f,
                                              std::abs(px - sun_x));
                float near_h = gfx::smoothstep(horizon_y + PH * 0.10f, horizon_y, py);
                Col warm = gfx::mix(Col{1.0f,0.65f,0.35f}, Col{1.0f,0.92f,0.7f},
                                    gfx::smoothstep(0.f, 18.f, sun_alt));
                col = gfx::add(col, gfx::scale(warm, spill * near_h * 0.28f * day));
            }
            // base vignette: gently darken the very bottom so the dashboard
            // cards read, without crushing the foreground meadow to flat black.
            float foot = gfx::smoothstep(PH * 0.90f, float(PH), py);
            col = gfx::scale(col, 1.f - foot * 0.30f);
            // Hard posterize — few flat bands, razor edges (pixel-art ground).
            return gfx::posterize(gfx::saturate(grade(col), 1.35f), GND_LEVELS);
        };

        // High-res render: each emitted sub-pixel is the average of several
        // horizontal samples, so curved/diagonal features (sun limb, hills,
        // clouds, stars) get smooth anti-aliasing instead of blocky steps.
        // Vertical resolution is the native 2 sub-pixels/cell; horizontal is
        // supersampled SSx×. On very wide terminals the per-pixel shader cost
        // (fBm clouds × every sample) dominates, so we drop to 1× there — the
        // AA loss on already-soft clouds/hills is negligible.
        const int SSx = (r.w > 160) ? 1 : 2;
        auto sample_row = [&](float cx_left, float sub_y) -> Col {
            Col acc{0,0,0};
            for (int s = 0; s < SSx; ++s) {
                float px = cx_left + (s + 0.5f) / SSx;
                Col c0 = shade(px, sub_y);
                acc.r += c0.r; acc.g += c0.g; acc.b += c0.b;
            }
            float inv = 1.f / SSx;
            return {acc.r * inv, acc.g * inv, acc.b * inv};
        };

        // ── Efficient, smooth sky: amortized incremental shading ──────────
        // The per-pixel shader (fbm clouds, glow, stars) is expensive, and the
        // scene is POSTERIZED — only ~25 distinct colours, motion happens in
        // discrete band steps. So two rules keep it buttery AND cheap:
        //   1. Never shade the whole grid in one frame (that was the 15-25ms
        //      hitch every 1/6s). Instead shade a SLICE of rows each frame and
        //      advance a cursor, so every frame costs roughly the same small
        //      amount and the picture refreshes continuously top-to-bottom.
        //   2. Blit the cached (already-posterized) colour directly — NO
        //      per-frame lerp + re-posterize. That churn re-emitted ~200
        //      cells/frame of invisible band-flicker; here a cell only changes
        //      on the wire when its posterized colour actually moved a band.
        const size_t need = size_t(r.w) * r.h;
        bool resized = cache_.size() != need * 2 || cw_ != r.w || ch_ != r.h;
        if (resized || dirty_) {
            dirty_ = false;
            cache_.assign(need * 2, Col{});
            cw_ = r.w; ch_ = r.h;
            shade_row_ = 0;
            // full first shade so the screen isn't blank for a few frames
            for (int cy = 0; cy < r.h; ++cy)
                for (int cx = 0; cx < r.w; ++cx) {
                    float xl = float(r.x + cx);
                    size_t i = (size_t(cy) * r.w + cx) * 2;
                    cache_[i]     = sample_row(xl, cy * 2 + 0.5f);
                    cache_[i + 1] = sample_row(xl, cy * 2 + 1.5f);
                }
        } else {
            // Refresh the whole grid every ~SWEEP_S seconds by shading
            // `rows_per_frame` rows per frame. Short sweep = band edges update
            // promptly (continuous-feeling motion) while no single frame shades
            // more than a few rows (no hitch). Capped so wide terminals stay cheap.
            constexpr float SWEEP_S = 0.10f;
            int rows_per_frame = std::clamp(
                (int)std::ceil(r.h / (SWEEP_S * 60.f)), 1, 10);
            for (int n = 0; n < rows_per_frame; ++n) {
                int cy = shade_row_;
                for (int cx = 0; cx < r.w; ++cx) {
                    float xl = float(r.x + cx);
                    size_t i = (size_t(cy) * r.w + cx) * 2;
                    cache_[i]     = sample_row(xl, cy * 2 + 0.5f);
                    cache_[i + 1] = sample_row(xl, cy * 2 + 1.5f);
                }
                shade_row_ = (shade_row_ + 1) % r.h;
            }
        }
        // Blit the cached posterized grid directly, every cell every frame.
        // This is what RESTORES sky cells that foreground widgets (clock digits,
        // cards) overpainted last frame and have since vacated — so a shadow-
        // diff here would strand stale glyphs. The blit itself is cheap (just a
        // style intern + grid store); maya's wire diff then drops the cells
        // identical to last frame, so only genuinely-changed cells hit the wire.
        for (int cy = 0; cy < r.h; ++cy)
            for (int cx = 0; cx < r.w; ++cx) {
                size_t i = (size_t(cy) * r.w + cx) * 2;
                p.cell(r.x + cx, r.y + cy, cache_[i], cache_[i + 1]);
            }

        // publish the live cell colours for foreground widgets to composite on
        g_sky_cells = &cache_; g_sky_w = r.w; g_sky_h = r.h;
        g_sky_ox = r.x; g_sky_oy = r.y;

        // ── bird flock (daytime overlay) ───────────────────────────────────
        // A small V-formation of distant birds drifting across the upper sky.
        // Drawn as moving glyph cells AFTER the cached sky blit (they move every
        // frame, so baking them into the slow shader cache would stutter). The
        // glyph's backdrop is sampled from the cached sky so they blend in.
        if (sun_alt > 2.f) {                      // only in real daylight
            float day = gfx::smoothstep(2.f, 12.f, sun_alt);
            // flock anchor sweeps left→right over ~80s, gently bobbing.
            float t = std::fmod(anim, 80.f) / 80.f;
            float ax = (t * 1.3f - 0.15f) * r.w;            // cells
            float ay = r.h * 0.18f + std::sin(anim * 0.25f) * (r.h * 0.04f);
            // formation: lead bird + four wing birds in a shallow V.
            const float off[5][2] = {{0,0},{-2,0.8f},{2,0.8f},{-4,1.6f},{4,1.6f}};
            Col ink = gfx::scale(Col{0.05f,0.06f,0.10f}, 1.f); // near-black silhouette
            for (auto& o : off) {
                int bx = (int)std::lround(ax + o[0]);
                int by = (int)std::lround(ay + o[1]);
                if (bx < 1 || bx >= r.w - 1 || by < 1 || by >= r.h - 1) continue;
                // gentle wing-flap: ‿ (relaxed) vs ⌃-ish via two glyphs.
                bool up = std::sin(anim * 6.f + o[0] * 1.7f) > 0.f;
                char32_t g = up ? U'˄' : U'ˇ';   // ˄ wings up  /  ˇ wings down
                size_t ci = (size_t(by) * r.w + bx) * 2;
                Col sky_here = ci + 1 < cache_.size()
                             ? gfx::mix(cache_[ci], cache_[ci + 1], 0.5f)
                             : Col{0.4f,0.6f,0.9f};
                Col fg = gfx::mix(sky_here, ink, 0.55f + 0.35f * day);
                p.glyph_cell(r.x + bx, r.y + by, g, fg, sky_here);
            }
        }

        // ── precipitation + fog overlay ─────────────────────────────────────
        // Drawn AFTER the cached sky blit because it moves every frame. We
        // composite per sub-pixel over the cached colours: rain = fast diagonal
        // streaks, snow = slow drifting flakes, fog = a soft grey veil thickest
        // near the ground. Cheap: a couple of hashed lookups per cell, only when
        // some precip is active.
        const WeatherViz& w = viz_;
        if (w.rain > 0.01f || w.snow > 0.01f || w.fog > 0.01f) {
            const int PWv = r.w, PHv = r.h * 2;
            const float horizon = PHv * 0.62f;
            // wind-driven slant for rain (and a gentle sway for snow)
            float slant = std::clamp((w.wind - 1.f) * 0.5f, -0.6f, 0.9f) + 0.25f;
            auto sky_at = [&](int cx, int cy) -> std::pair<Col,Col> {
                size_t i = (size_t(cy) * r.w + cx) * 2;
                if (i + 1 < cache_.size()) return {cache_[i], cache_[i + 1]};
                return {Col{0.2f,0.3f,0.5f}, Col{0.2f,0.3f,0.5f}};
            };
            // rain streak field: a moving column of fast vertical noise. A cell
            // lights up where a hashed "drop" passes through it this frame.
            auto precip = [&](float px, float py) -> Col {
                Col add{0,0,0};
                if (py > horizon + 2.f) return add;          // no precip into the ground
                if (w.rain > 0.01f) {
                    float fall = anim * 38.f;               // drops/sec descent
                    float col_x = px - py * slant;           // slanted columns
                    float lane  = std::floor(col_x * 0.5f);
                    float seed  = gfx::hash2(lane, 11.3f);
                    if (seed < 0.30f + 0.55f * w.rain) {     // denser = more lanes
                        float speed = 0.8f + 0.5f * gfx::hash2(lane, 4.1f);
                        float phase = py + fall * speed + seed * 50.f;
                        float seg   = phase - std::floor(phase / 5.f) * 5.f; // 0..5 streak
                        float streak = gfx::smoothstep(2.2f, 0.f, seg) *
                                       gfx::smoothstep(0.9f, 0.f, std::abs(col_x - (lane*2.f+1.f)));
                        add = gfx::add(add, gfx::scale(Col{0.62f,0.72f,0.92f},
                                       streak * (0.30f + 0.45f * w.rain)));
                    }
                }
                if (w.snow > 0.01f) {
                    float fall = anim * 9.f;
                    // flakes sway sideways as they fall (sinusoidal drift)
                    float drift = std::sin((py * 0.20f) + anim * 1.2f) * 2.0f;
                    float fx = px + drift;
                    float cellx = std::floor(fx * 0.5f);
                    float row   = std::floor((py + fall) * 0.4f);
                    float seed  = gfx::hash2(cellx, row * 1.7f);
                    if (seed < 0.10f + 0.30f * w.snow) {
                        float sxp = gfx::hash2(cellx * 3.1f, row);
                        float fxx = (cellx * 2.f) + sxp * 2.f;
                        float fyy = std::floor(py) + 0.5f;
                        float d = std::hypot(fx - fxx, py - fyy);
                        float flake = gfx::smoothstep(1.0f, 0.f, d);
                        add = gfx::add(add, gfx::scale(Col{0.92f,0.95f,1.0f},
                                       flake * (0.55f + 0.40f * w.snow)));
                    }
                }
                return add;
            };
            for (int cy = 0; cy < r.h; ++cy) {
                for (int cx = 0; cx < r.w; ++cx) {
                    float px = float(cx);
                    float py0 = cy * 2.f + 0.5f, py1 = cy * 2.f + 1.5f;
                    Col r0 = precip(px, py0), r1 = precip(px, py1);
                    // fog: a grey veil whose density rises toward the horizon
                    // band and washes out colour. Animated with a slow breathing
                    // noise so it drifts.
                    float f0 = 0.f, f1 = 0.f;
                    if (w.fog > 0.01f) {
                        auto fogv = [&](float py){
                            float nearH = gfx::smoothstep(horizon * 0.30f, horizon, py)
                                        * gfx::smoothstep(horizon + PHv*0.18f, horizon*0.7f, py);
                            float n = gfx::fbm(px*0.05f - anim*0.06f, py*0.05f + 3.f);
                            return std::clamp(w.fog * (0.45f + 0.55f*nearH) * (0.6f+0.5f*n), 0.f, 0.9f);
                        };
                        f0 = fogv(py0); f1 = fogv(py1);
                    }
                    bool any = (r0.r+r0.g+r0.b) > 0.002f || (r1.r+r1.g+r1.b) > 0.002f
                             || f0 > 0.002f || f1 > 0.002f;
                    if (!any) continue;
                    auto [s0, s1] = sky_at(cx, cy);
                    Col fog_col{0.62f,0.64f,0.70f};
                    Col o0 = gfx::add(gfx::mix(s0, fog_col, f0), r0);
                    Col o1 = gfx::add(gfx::mix(s1, fog_col, f1), r1);
                    p.cell(r.x + cx, r.y + cy, o0, o1);
                }
            }
        }
    }

private:
    std::vector<Col> cache_;   // shaded + posterized colours, 2 sub-pixels/cell
    int  cw_ = -1, ch_ = -1;
    int  shade_row_ = 0;       // incremental-shade cursor (rows swept per frame)
    bool dirty_ = false;       // full reshade requested (instant response to input)
    WeatherViz viz_{};         // eased weather dials (cross-fades between codes)
    float last_anim_ = 0.f;    // previous frame clock, for frame-rate-independent ease
    float frozen_anim_ = 0.f;  // scenery anim clock, held still while warping
    float shaded_cloud_ = -1.f, shaded_storm_ = -1.f;  // last values baked into cache_
    float shaded_sunalt_ = -999.f;                     // sun altitude baked into cache_
    float levels_ = 8.f;       // posterize depth: few flat bands = pixel-art look
};

// helper other widgets use to scrim text legibly over the sky
inline Col sky_scrim(float sun_alt, int cell_y, int pix_h) {
    float v = 1.f - float(cell_y * 2 + 1) / float(pix_h);
    return gfx::mix(sky_palette(sun_alt, v), Col{0,0,0}, 0.40f);
}

// the plain sky colour behind a cell (no darkening) — used as the AA backdrop
// for large glyphs that carry their own contrast via a drop shadow.
inline Col sky_bg(float sun_alt, int cell_y, int pix_h) {
    float v = 1.f - float(cell_y * 2 + 1) / float(pix_h);
    return sky_palette(sun_alt, v);
}

} // namespace chronos::ui
