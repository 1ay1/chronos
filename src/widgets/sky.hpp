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

                    // crepuscular rays: angular shafts of light streaming out
                    // from the disc. The angle to the sun keys a slowly-rotating
                    // band of noise so the shafts shimmer and drift; they fade
                    // with distance and are strongest at low (golden) sun.
                    {
                        float ang = std::atan2(py - sun_y, px - sun_x);
                        float beams = gfx::fbm(ang * 2.4f + c.anim * 0.05f, ds * 0.012f);
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
                                       pt * (0.4f + bright) * tw * star_show));
                    }

                    // shooting stars: every ~6s a meteor crosses the sky. The
                    // epoch index seeds a random entry point + slope; within the
                    // epoch a 0..1 progress sweeps the head along the streak and
                    // a short bright tail trails behind it, fading out.
                    {
                        float T = c.anim / 6.0f;
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
                    // sky) — saves ~5 fBm calls/pixel over the bottom third.
                    if (band < 0.01f && cirrus_band0 < 0.01f) return col;
                    float day  = gfx::smoothstep(-6.f, 8.f, sun_alt);
                    float wind = c.anim * 2.0f;   // horizontal drift of the air mass

                    // normalised cloud-space coords (decouple shape from term size)
                    float u = px * 0.030f, v = py * 0.075f;

                    // ── domain warp: offset the lookup by a low-freq flow field.
                    // This bends the noise into billows instead of round blobs.
                    float wx = gfx::fbm(u * 0.5f - wind * 0.010f, v * 0.5f + 3.1f);
                    float wy = gfx::fbm(u * 0.5f + 5.7f, v * 0.5f - wind * 0.008f);
                    float warp = 1.6f;

                    // ── low cumulus deck: fat, slow, billowing ────────────────
                    float cu = gfx::fbm(u + (wx - 0.5f) * warp - wind * 0.018f,
                                        v + (wy - 0.5f) * warp + 12.3f);
                    // sharpen tops: cauliflower bias makes crests bulge upward
                    cu = cu * 0.7f + gfx::fbm(u * 2.1f - wind * 0.026f,
                                              v * 2.1f + 7.0f) * 0.3f;
                    float cumulus = gfx::smoothstep(0.54f, 0.60f, cu) * band;

                    // ── high cirrus deck: thin, fast, stretched horizontally ──
                    float ci = gfx::fbm(u * 0.6f - wind * 0.075f,
                                        v * 2.4f + 40.0f);   // y-stretched = streaky
                    float cirrus = gfx::smoothstep(0.62f, 0.70f, ci)
                                 * cirrus_band0 * 0.6f;

                    // composite (cumulus dominates where present)
                    float dens = std::max(cumulus, cirrus * 0.8f);
                    if (dens > 0.008f) {
                        // self-shadow: sample density a step toward the sun; if
                        // it's denser there, this pixel sits in the cloud's shade.
                        float sdx = (sun_x - px), sdy = (sun_y - py);
                        float sl = 1.f / (std::hypot(sdx, sdy) + 1e-3f);
                        float ahead = gfx::fbm(u + (wx - 0.5f) * warp - wind * 0.018f
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
                }
                return gfx::posterize(gfx::saturate(col, 1.25f), 8.f);
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
            return gfx::posterize(gfx::saturate(col, 1.35f), 7.f);
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

        // The sky shader is expensive (per-pixel fbm clouds, glow, stars). It
        // also evolves slowly, so we only RE-SHADE ~6×/sec. But blitting the
        // same cached grid until the next shade makes motion jump (the lag).
        // Fix: keep the LAST two shaded keyframes and temporally INTERPOLATE
        // between them every frame by the fractional progress past the current
        // keyframe. The heavy shader stays at 6fps; the displayed frame slides
        // continuously at the full 30fps — smooth motion at no extra shade cost.
        const size_t need = size_t(r.w) * r.h;
        bool resized = cache_.size() != need * 2 || cw_ != r.w || ch_ != r.h;
        // keyframe index advances ~6×/sec; sun altitude quantised to 0.05°.
        constexpr float KF_HZ = 6.f;
        long anim_q = (long)std::floor(c.anim * KF_HZ);
        long sun_q  = (long)std::floor(sun_alt * 20.f);
        bool new_kf = (anim_q != anim_q_ || sun_q != sun_q_);
        if (resized) {
            cache_.assign(need * 2, Col{});
            cache_prev_.assign(need * 2, Col{});
            cw_ = r.w; ch_ = r.h;
            anim_q_ = LONG_MIN;   // force a fresh shade below
            new_kf = true;
        }
        if (new_kf) {
            std::swap(cache_prev_, cache_);   // old current → prev
            anim_q_ = anim_q; sun_q_ = sun_q;
            for (int cy = 0; cy < r.h; ++cy)
                for (int cx = 0; cx < r.w; ++cx) {
                    float xl = float(r.x + cx);
                    size_t i = (size_t(cy) * r.w + cx) * 2;
                    cache_[i]     = sample_row(xl, cy * 2 + 0.5f);
                    cache_[i + 1] = sample_row(xl, cy * 2 + 1.5f);
                }
            if (resized)  // first shade has no valid prev — seed it equal
                cache_prev_ = cache_;
        }
        // fractional time since the current keyframe began, 0..1 across one KF.
        float frac = std::clamp((c.anim * KF_HZ) - float(anim_q_), 0.f, 1.f);
        // Blit prev→cur interpolated. maya's cell diff drops cells identical to
        // last frame, so the wire cost is only the cells that actually changed.
        for (int cy = 0; cy < r.h; ++cy)
            for (int cx = 0; cx < r.w; ++cx) {
                size_t i = (size_t(cy) * r.w + cx) * 2;
                // interpolate between keyframes, then RE-POSTERIZE so the lerp
                // doesn't smear the crisp flat bands back into a gradient.
                Col top = gfx::posterize(gfx::mix(cache_prev_[i],   cache_[i],   frac), 8.f);
                Col bot = gfx::posterize(gfx::mix(cache_prev_[i+1], cache_[i+1], frac), 8.f);
                p.cell(r.x + cx, r.y + cy, top, bot);
            }

        // ── bird flock (daytime overlay) ───────────────────────────────────
        // A small V-formation of distant birds drifting across the upper sky.
        // Drawn as moving glyph cells AFTER the cached sky blit (they move every
        // frame, so baking them into the slow shader cache would stutter). The
        // glyph's backdrop is sampled from the cached sky so they blend in.
        if (sun_alt > 2.f) {                      // only in real daylight
            float day = gfx::smoothstep(2.f, 12.f, sun_alt);
            // flock anchor sweeps left→right over ~80s, gently bobbing.
            float t = std::fmod(c.anim, 80.f) / 80.f;
            float ax = (t * 1.3f - 0.15f) * r.w;            // cells
            float ay = r.h * 0.18f + std::sin(c.anim * 0.25f) * (r.h * 0.04f);
            // formation: lead bird + four wing birds in a shallow V.
            const float off[5][2] = {{0,0},{-2,0.8f},{2,0.8f},{-4,1.6f},{4,1.6f}};
            Col ink = gfx::scale(Col{0.05f,0.06f,0.10f}, 1.f); // near-black silhouette
            for (auto& o : off) {
                int bx = (int)std::lround(ax + o[0]);
                int by = (int)std::lround(ay + o[1]);
                if (bx < 1 || bx >= r.w - 1 || by < 1 || by >= r.h - 1) continue;
                // gentle wing-flap: ‿ (relaxed) vs ⌃-ish via two glyphs.
                bool up = std::sin(c.anim * 6.f + o[0] * 1.7f) > 0.f;
                char32_t g = up ? U'˄' : U'ˇ';   // ˄ wings up  /  ˇ wings down
                size_t ci = (size_t(by) * r.w + bx) * 2;
                Col sky_here = ci + 1 < cache_.size()
                             ? gfx::mix(cache_[ci], cache_[ci + 1], 0.5f)
                             : Col{0.4f,0.6f,0.9f};
                Col fg = gfx::mix(sky_here, ink, 0.55f + 0.35f * day);
                p.glyph_cell(r.x + bx, r.y + by, g, fg, sky_here);
            }
        }
    }

private:
    std::vector<Col> cache_;        // current shaded keyframe
    std::vector<Col> cache_prev_;   // previous keyframe (for temporal lerp)
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
