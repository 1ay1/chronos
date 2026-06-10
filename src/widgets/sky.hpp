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
                // stars: varied brightness/colour + a faint Milky-Way band
                if (star_vis > 0.01f && py < horizon_y * 0.98f) {
                    // Milky-Way: a soft diagonal band of unresolved glow
                    float mwd = std::abs((py - horizon_y * 0.35f) - (px - PW * 0.5f) * 0.28f);
                    float mw = gfx::smoothstep(PH * 0.16f, 0.f, mwd);
                    float mwn = gfx::fbm(px * 0.06f, py * 0.12f + 5.f);
                    col = gfx::add(col, gfx::scale(Col{0.18f,0.20f,0.30f},
                                   mw * mwn * 0.5f * star_vis));
                    // discrete stars on a fine grid, sub-cell positioned
                    float gx = std::floor(px), gy = std::floor(py);
                    float hs = gfx::hash2(gx * 1.7f, gy * 2.3f);
                    if (hs > 0.972f) {
                        float bright = (hs - 0.972f) / 0.028f;       // 0..1
                        float tw = 0.55f + 0.45f * std::sin(c.anim * 2.5f + hs * 60.f);
                        // colour by a second hash: warm vs cool stars
                        float hc = gfx::hash2(gy * 3.1f, gx * 1.3f);
                        Col scol = gfx::mix(Col{1.0f,0.85f,0.7f}, Col{0.75f,0.85f,1.0f}, hc);
                        // sub-pixel falloff for a clean point
                        float sxp = gfx::hash2(gx * 5.1f, gy * 7.3f);
                        float syp = gfx::hash2(gx * 7.7f, gy * 3.9f);
                        float dpt = std::hypot(px - (gx + sxp), py - (gy + syp));
                        float pt = gfx::smoothstep(0.9f + bright * 0.6f, 0.f, dpt);
                        col = gfx::add(col, gfx::scale(scol,
                                       pt * (0.4f + bright) * tw * star_vis));
                    }
                }
                // clouds: two volumetric layers, lit from the sun side
                {
                    float band = gfx::smoothstep(horizon_y, horizon_y * 0.20f, py);
                    float day = gfx::smoothstep(-6.f, 8.f, sun_alt);
                    // density field (low + high octave drift at different speeds)
                    float d0 = gfx::fbm(px * 0.030f + c.anim * 0.05f, py * 0.075f + 12.3f);
                    float d1 = gfx::fbm(px * 0.075f - c.anim * 0.08f, py * 0.150f + 40.0f);
                    float dens = gfx::smoothstep(0.52f, 0.80f, d0 * 0.65f + d1 * 0.35f) * band;
                    if (dens > 0.01f) {
                        // self-shadow: sample density a step toward the sun; if it's
                        // denser there, this cloud pixel is in shadow (darker).
                        float sdx = (sun_x - px), sdy = (sun_y - py);
                        float sl = 1.f / (std::hypot(sdx, sdy) + 1e-3f);
                        float ahead = gfx::fbm((px + sdx * sl * 6.f) * 0.030f + c.anim * 0.05f,
                                               (py + sdy * sl * 6.f) * 0.075f + 12.3f);
                        float lit = std::clamp(1.0f - (ahead - d0) * 2.2f, 0.25f, 1.f);
                        Col lo = gfx::mix(Col{0.06f,0.07f,0.13f}, Col{0.30f,0.30f,0.40f}, day);
                        Col hi = gfx::mix(Col{0.55f,0.40f,0.42f}, Col{1.0f,0.99f,0.98f},
                                          gfx::smoothstep(-6.f, 4.f, sun_alt));
                        Col cc = gfx::mix(lo, hi, lit);
                        // warm rim where clouds catch low sun
                        if (sun_alt > -4.f && sun_alt < 14.f) {
                            float rim = gfx::smoothstep(0.55f, 0.62f, dens) *
                                        (1.f - gfx::smoothstep(0.62f, 0.72f, dens));
                            cc = gfx::add(cc, gfx::scale(Col{1.0f,0.55f,0.30f}, rim * 0.6f));
                        }
                        col = gfx::mix(col, cc, std::min(dens * 1.05f, 0.96f));
                    }
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

        // High-res render: each emitted sub-pixel is the average of several
        // horizontal samples, so curved/diagonal features (sun limb, hills,
        // clouds, stars) get smooth anti-aliasing instead of blocky steps.
        // Vertical resolution is the native 2 sub-pixels/cell; horizontal is
        // supersampled SSx×.
        constexpr int SSx = 2;
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

        // The sky shader is expensive (per-pixel fbm clouds, glow, stars). It
        // also evolves slowly — sun crawls, clouds drift, stars twinkle gently.
        // So we cache the computed top/bot colour per cell and only recompute
        // when the scene meaningfully changed: a resize, or ~8 sky-updates/sec
        // of animation progress. maya's cell diff then only re-emits the few
        // cells that actually changed between cached grids — paint stays cheap
        // at 30fps while the heavy shading runs ~8fps.
        const size_t need = size_t(r.w) * r.h;
        bool stale = cache_.size() != need * 2 || cw_ != r.w || ch_ != r.h;
        // animation clock quantised to ~1/3 s; sun altitude quantised to 0.05°.
        long anim_q = (long)std::floor(c.anim * 3.f);
        long sun_q  = (long)std::floor(sun_alt * 20.f);
        if (anim_q != anim_q_ || sun_q != sun_q_) stale = true;
        if (stale) {
            cache_.resize(need * 2);
            cw_ = r.w; ch_ = r.h; anim_q_ = anim_q; sun_q_ = sun_q;
            for (int cy = 0; cy < r.h; ++cy)
                for (int cx = 0; cx < r.w; ++cx) {
                    float xl = float(r.x + cx);
                    size_t i = (size_t(cy) * r.w + cx) * 2;
                    cache_[i]     = sample_row(xl, cy * 2 + 0.5f);
                    cache_[i + 1] = sample_row(xl, cy * 2 + 1.5f);
                }
        }
        // Re-blit the cached grid every frame. The expensive part (the shader)
        // is gated by `stale`; the blit just re-asserts the sky colours so any
        // cell a foreground widget vacated this frame (shrinking clock digits,
        // dismissed modal) is correctly restored to sky. maya's cell diff drops
        // the cells identical to last frame, so the wire cost is only the few
        // that actually changed.
        for (int cy = 0; cy < r.h; ++cy)
            for (int cx = 0; cx < r.w; ++cx) {
                size_t i = (size_t(cy) * r.w + cx) * 2;
                p.cell(r.x + cx, r.y + cy, cache_[i], cache_[i + 1]);
            }
    }

private:
    std::vector<Col> cache_;
    int  cw_ = -1, ch_ = -1;
    long anim_q_ = LONG_MIN, sun_q_ = LONG_MIN;
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
