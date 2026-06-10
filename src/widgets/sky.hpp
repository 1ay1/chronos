#pragma once
// chronos::ui::SkyWidget — the living sky background.
//
// Paints a per-pixel gradient driven by the sun's true altitude, plus a sun
// disc with glow, a phase-shaded moon, twinkling stars, drifting fBm clouds,
// and a rolling-hill horizon silhouette. This is the canvas everything else
// floats over.

#include "../widget.hpp"
#include <cmath>

namespace chronos::ui {

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

class SkyWidget : public Widget {
public:
    const char* name() const override { return "sky"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        const int PW = r.w, PH = r.h * 2;
        const float horizon_y = PH * 0.62f;
        const float sun_alt = (float)c.sun.altitude;
        const bool  night = sun_alt < -6.f;
        const float star_vis = gfx::smoothstep(0.f, -12.f, sun_alt);

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

        auto shade = [&](float px, float py) -> Col {
            float vv = 1.f - py / float(PH);
            if (py < horizon_y) {
                Col col = sky_palette(sun_alt, vv);

                // sun
                if (sun_alt > -2.f) {
                    float ds = std::hypot(px - sun_x, py - sun_y);
                    float disc = gfx::smoothstep(3.2f, 2.0f, ds);
                    float glow = gfx::smoothstep(PW * 0.5f, 0.f, ds) * 0.45f;
                    Col sc = gfx::mix(Col{1,0.95f,0.75f}, Col{1,0.78f,0.45f},
                                      gfx::smoothstep(20.f, 0.f, sun_alt));
                    col = gfx::add(col, gfx::scale(sc, glow));
                    col = gfx::mix(col, Col{1,0.98f,0.9f}, disc);
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
                // stars
                if (star_vis > 0.01f && py < horizon_y * 0.95f) {
                    float hs = gfx::hash2(std::floor(px) * 1.7f, std::floor(py) * 2.3f);
                    if (hs > 0.985f) {
                        float tw = 0.6f + 0.4f * std::sin(c.anim * 3.f + hs * 50.f);
                        col = gfx::add(col, gfx::scale(Col{0.9f,0.92f,1.f},
                                       (hs - 0.985f) / 0.015f * tw * star_vis));
                    }
                }
                // clouds
                float band = gfx::smoothstep(horizon_y, horizon_y * 0.25f, py);
                float n = gfx::fbm(px * 0.05f + c.anim * 0.06f, py * 0.10f + 12.3f);
                float cl = gfx::smoothstep(0.55f, 0.78f, n) * band;
                if (cl > 0.01f) {
                    float day = gfx::smoothstep(-4.f, 8.f, sun_alt);
                    Col cc = gfx::mix(Col{0.10f,0.11f,0.18f},
                              gfx::mix(Col{0.85f,0.55f,0.45f}, Col{0.95f,0.95f,0.98f}, day),
                              gfx::smoothstep(-10.f, 2.f, sun_alt));
                    col = gfx::mix(col, cc, cl * 0.85f);
                }
                return col;
            }
            // ground — layered rolling hills with atmospheric depth.
            // Sky colour at the horizon, used to tint distant ridges (haze).
            Col horizon_col = sky_palette(sun_alt, 0.f);
            float day = gfx::smoothstep(-8.f, 6.f, sun_alt);   // 0 night .. 1 day
            float sun_dir = std::clamp((sun_x / PW - 0.5f) * 2.f, -1.f, 1.f);

            // Three ridges: far (hazy, high) → near (saturated, low).
            struct Ridge { float base; float amp; float freq; float phase; Col lo; Col hi; };
            const Ridge ridges[] = {
                // far blue-grey ridge, blends with haze
                { horizon_y + PH * 0.020f, 2.5f, 0.018f, 0.4f,
                  {0.10f,0.13f,0.18f}, {0.28f,0.34f,0.42f} },
                // mid green-grey slope
                { horizon_y + PH * 0.075f, 4.0f, 0.030f, 2.1f,
                  {0.05f,0.09f,0.07f}, {0.16f,0.30f,0.18f} },
                // near foreground, darkest & most saturated
                { horizon_y + PH * 0.170f, 6.0f, 0.045f, 4.7f,
                  {0.03f,0.06f,0.04f}, {0.10f,0.22f,0.12f} },
            };

            Col col = horizon_col;            // start from the sky we sit under
            bool drawn = false;
            for (const Ridge& rg : ridges) {
                float crest = rg.base
                            + rg.amp * std::sin(px * rg.freq + rg.phase)
                            + rg.amp * 0.5f * std::sin(px * rg.freq * 2.3f + rg.phase * 1.7f);
                if (py < crest) continue;     // above this ridge's silhouette
                drawn = true;
                // shading: brighter on the slope facing the sun, fades with depth.
                float slope = std::cos(px * rg.freq + rg.phase);  // -1..1 ridge facing
                float facing = 0.5f + 0.5f * slope * sun_dir;
                float lo_t = std::clamp((py - crest) / (PH * 0.10f), 0.f, 1.f);
                Col terr = gfx::mix(rg.hi, rg.lo, lo_t);
                // sunlit warmth on facing slopes during the day
                terr = gfx::mix(terr, gfx::add(terr, Col{0.18f,0.14f,0.05f}),
                                facing * day * 0.7f);
                // atmospheric haze: distant (high crest) ridges wash toward sky
                float haze = gfx::smoothstep(horizon_y + PH * 0.18f,
                                             horizon_y, crest);
                terr = gfx::mix(terr, horizon_col, haze * (0.35f + 0.35f * day));
                col = terr;
            }
            if (!drawn) {
                // a thin lit rim right at the horizon line before the first ridge
                float rim = gfx::smoothstep(horizon_y + PH * 0.03f, horizon_y, py);
                col = gfx::mix(horizon_col, gfx::scale(horizon_col, 0.6f), rim);
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
            // base vignette: darken the very bottom so the dashboard cards read
            float foot = gfx::smoothstep(PH * 0.86f, float(PH), py);
            col = gfx::scale(col, 1.f - foot * 0.45f);
            return col;
        };

        for (int cy = 0; cy < r.h; ++cy)
            for (int cx = 0; cx < r.w; ++cx) {
                float px = r.x + cx + 0.5f;
                p.cell(r.x + cx, r.y + cy,
                       shade(px, cy * 2 + 0.f), shade(px, cy * 2 + 1.f));
            }
    }
};

// helper other widgets use to scrim text legibly over the sky
inline Col sky_scrim(float sun_alt, int cell_y, int pix_h) {
    float v = 1.f - float(cell_y * 2 + 1) / float(pix_h);
    return gfx::mix(sky_palette(sun_alt, v), Col{0,0,0}, 0.40f);
}

} // namespace chronos::ui
