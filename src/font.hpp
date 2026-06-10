#pragma once
// chronos::font — a scalable anti-aliased stroke font for the half-block canvas.
//
// Glyphs are authored once as STROKES (polylines) in a normalized 0..1 em-box.
// At draw time we rasterize each stroke as a thick, round-capped line using a
// signed-distance field, supersampled per sub-pixel, so the SAME glyph renders
// crisp and smooth at ANY pixel height — 8px or 80px. No bitmaps, no fixed
// grid. This is the font everything graphical in chronos draws with.
//
// Coordinates: (0,0) top-left of the em-box, (1,1) bottom-right. A glyph's
// advance width is its own `adv` (also in em units). Stroke thickness is given
// as a fraction of the em height, so weight scales with size.

#include "gfx.hpp"
#include <array>
#include <cmath>
#include <initializer_list>
#include <span>
#include <vector>

namespace chronos::font {

using gfx::Col;

struct Pt { float x, y; };

// A glyph: a list of polyline strokes plus an advance width (em units).
struct Glyph {
    float adv = 0.6f;
    std::vector<std::vector<Pt>> strokes;   // each inner vector is one polyline
    std::vector<std::array<Pt,4>> fills;     // optional filled quads (rare)
};

// ── distance from point p to segment ab ─────────────────────────────────────
inline float seg_dist(float px, float py, Pt a, Pt b) {
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = px - a.x, wy = py - a.y;
    float vv = vx * vx + vy * vy;
    float t = vv > 1e-9f ? std::clamp((wx * vx + wy * vy) / vv, 0.f, 1.f) : 0.f;
    float dx = px - (a.x + t * vx), dy = py - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

// ── the glyph table (digits, colon, a few letters/symbols we need) ──────────
// Authored on a tasteful geometric grid: x in [0.10,0.90], y in [0.06,0.94].
namespace tbl {

// sample an elliptical arc (cx,cy radius rx,ry) from angle a0..a1 (radians,
// 0 = +x, CCW) into `dst`. y grows downward so we negate sin.
inline void arc(std::vector<Pt>& dst, float cx, float cy, float rx, float ry,
                float a0, float a1, int seg = 14) {
    for (int i = 0; i <= seg; ++i) {
        float t = a0 + (a1 - a0) * (float(i) / seg);
        dst.push_back({cx + rx * std::cos(t), cy - ry * std::sin(t)});
    }
}
constexpr float PI = 3.14159265f;

// Geometric face. Pen weight is uniform; curves come from sampled arcs so the
// strokes never self-intersect into blobs.
inline Glyph make(char32_t cp) {
    const float L = 0.18f, R = 0.82f, M = 0.5f;     // left / right / mid x
    const float T = 0.10f, C = 0.5f,  B = 0.90f;    // top / center / bottom y
    const float rx = (R - L) / 2.f;                 // half width
    const float qy = (C - T);                       // half height
    Glyph g; g.adv = 0.72f;
    switch (cp) {
    case U'0': {
        std::vector<Pt> o;
        arc(o, M, C, rx, qy, PI/2, PI/2 + 2*PI, 28);   // full ellipse
        g.strokes = { o, { {R-0.08f,T+0.10f},{L+0.08f,B-0.10f} } };  // slash
        break; }
    case U'1':
        g.adv = 0.46f;
        g.strokes = { { {L+0.02f,T+0.16f},{M,T},{M,B} }, { {L-0.02f,B},{R-0.10f,B} } };
        break;
    case U'2': {
        std::vector<Pt> s; arc(s, M, T+qy*0.55f, rx, qy*0.55f, PI*0.92f, -PI*0.30f, 16);
        s.push_back({L, B}); s.push_back({R, B});
        g.strokes = { s };
        break; }
    case U'3': {
        std::vector<Pt> s;
        arc(s, M, T+qy*0.5f, rx, qy*0.5f, PI*0.95f, -PI*0.55f, 16);
        arc(s, M, B-qy*0.5f, rx, qy*0.5f, PI*0.55f, -PI*0.95f, 16);
        g.strokes = { s };
        break; }
    case U'4':
        g.strokes = { { {R-0.12f,B},{R-0.12f,T} },
                      { {R-0.12f,T},{L-0.02f,C+0.14f},{R+0.02f,C+0.14f} } };
        break;
    case U'5': {
        std::vector<Pt> s = { {R-0.02f,T},{L,T},{L,C-0.02f} };
        arc(s, M, C+qy*0.4f, rx, qy*0.55f, PI*0.62f, -PI*0.95f, 18);
        g.strokes = { s };
        break; }
    case U'6': {
        std::vector<Pt> o; arc(o, M, B-qy*0.55f, rx, qy*0.55f, 0, 2*PI, 24);  // lower bowl
        std::vector<Pt> tail; arc(tail, M, C+0.02f, rx, qy*1.1f, PI*0.5f, PI*1.05f, 14);
        g.strokes = { o, tail };
        break; }
    case U'7':
        g.strokes = { { {L,T},{R,T},{M-0.04f,B} } };
        break;
    case U'8': {
        std::vector<Pt> top; arc(top, M, T+qy*0.5f, rx*0.86f, qy*0.5f, 0, 2*PI, 22);
        std::vector<Pt> bot; arc(bot, M, B-qy*0.5f, rx, qy*0.5f, 0, 2*PI, 24);
        g.strokes = { top, bot };
        break; }
    case U'9': {
        std::vector<Pt> o; arc(o, M, T+qy*0.55f, rx, qy*0.55f, 0, 2*PI, 24);  // upper bowl
        std::vector<Pt> tail; arc(tail, M, C-0.02f, rx, qy*1.1f, -PI*0.5f, PI*0.05f, 14);
        g.strokes = { o, tail };
        break; }
    case U':':
        g.adv = 0.30f;
        g.strokes = { { {M,T+qy*0.7f},{M,T+qy*0.7f+0.001f} },
                      { {M,B-qy*0.7f},{M,B-qy*0.7f+0.001f} } };  // two dots
        break;
    case U'.':
        g.adv = 0.28f;
        g.strokes = { { {M,B-0.02f} } };
        break;
    case U'-':
        g.adv = 0.5f;
        g.strokes = { { {L,C},{R,C} } };
        break;
    case U' ':
        g.adv = 0.40f;
        break;
    default:
        g.adv = 0.40f;
        break;
    }
    return g;
}

} // namespace tbl

// glyph cache so we don't rebuild the polylines every frame.
inline const Glyph& glyph(char32_t cp) {
    static std::array<Glyph, 128> ascii;
    static std::array<bool, 128> built{};
    if (cp < 128) {
        if (!built[cp]) { ascii[cp] = tbl::make(cp); built[cp] = true; }
        return ascii[cp];
    }
    static Glyph fallback = tbl::make(U' ');
    return fallback;
}

// ── measure a string's advance in em units ──────────────────────────────────
inline float measure_em(std::string_view s) {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        char32_t cp; i += gfx::utf8_decode(s, i, cp);
        w += glyph(cp).adv + 0.06f;   // tracking
    }
    return w > 0 ? w - 0.06f : 0;
}

// ════════════════════════════════════════════════════════════════════════
//  draw_text — rasterize a string at a given pixel HEIGHT, AA, any size.
//
//  px,py     : top-left in SUB-PIXEL coords (canvas is cols × rows*2 px)
//  height_px : em-box height in sub-pixels (the visual cap height ~ this)
//  weight    : stroke thickness as a fraction of height (0.10 ≈ medium)
//  fg        : ink colour;  bg_fn(cx,cy)->Col samples the backdrop per cell.
//
//  Returns the advanced x in sub-pixels (so callers can chain / right-align).
// ════════════════════════════════════════════════════════════════════════
template <class BgFn>
inline float draw_text(gfx::Painter& p, float px, float py, float height_px,
                       std::string_view s, Col fg, BgFn&& bg_fn,
                       float weight = 0.11f) {
    const float em = height_px;                 // px per em unit
    const float half = std::max(0.6f, weight * em * 0.5f);  // half stroke width

    // Pre-decode glyphs + pen positions so we know the pixel bbox to scan.
    struct Placed { const Glyph* g; float ox; };
    std::vector<Placed> placed;
    float pen = px;
    for (size_t i = 0; i < s.size();) {
        char32_t cp; i += gfx::utf8_decode(s, i, cp);
        const Glyph& g = glyph(cp);
        placed.push_back({&g, pen});
        pen += (g.adv + 0.06f) * em;
    }
    float x0 = px, x1 = pen;
    float y0 = py, y1 = py + em;

    // sub-pixel bbox → cell bbox
    int cx0 = std::max(0, int(std::floor(x0)) - 1);
    int cx1 = std::min(p.pix_w() - 1, int(std::ceil(x1)) + 1);
    int ry0 = std::max(0, int(std::floor(y0 / 2.f)) - 1);
    int ry1 = std::min(p.rows() - 1, int(std::ceil(y1 / 2.f)) + 1);

    // coverage of sub-pixel (sx,sy) against all strokes of all glyphs.
    auto cover = [&](float sx, float sy) -> float {
        float best = 1e9f;
        for (const Placed& pl : placed) {
            // transform sub-pixel into this glyph's em box
            float gx = (sx - pl.ox) / em;
            float gy = (sy - py) / em;
            if (gx < -0.3f || gx > pl.g->adv + 0.3f || gy < -0.3f || gy > 1.3f)
                continue;
            for (const auto& stroke : pl.g->strokes) {
                for (size_t k = 0; k + 1 < stroke.size(); ++k) {
                    float d = seg_dist(gx, gy, stroke[k], stroke[k + 1]) * em;
                    best = std::min(best, d);
                }
                if (stroke.size() == 1) {  // a dot
                    float dx = (gx - stroke[0].x) * em, dy = (gy - stroke[0].y) * em;
                    best = std::min(best, std::sqrt(dx*dx + dy*dy));
                }
            }
        }
        // AA: 1px feather around the half-width edge
        return gfx::smoothstep(half + 0.75f, half - 0.75f, best);
    };

    // 2× supersample each sub-pixel (2x2) for smoother edges, cheap at this scale.
    for (int cy = ry0; cy <= ry1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            Col bg = bg_fn(cx, cy);
            Col top = bg, bot = bg;
            // top sub-pixel = canvas row cy*2, bottom = cy*2+1
            for (int half_i = 0; half_i < 2; ++half_i) {
                float base_y = cy * 2 + half_i;
                float a = 0;
                for (int sj = 0; sj < 2; ++sj)
                    for (int si = 0; si < 2; ++si)
                        a += cover(cx + 0.25f + si * 0.5f, base_y + 0.25f + sj * 0.5f);
                a *= 0.25f;
                if (a > 0.004f) {
                    Col blended = gfx::mix(bg, fg, a);
                    (half_i == 0 ? top : bot) = blended;
                }
            }
            if (top.r != bg.r || top.g != bg.g || top.b != bg.b ||
                bot.r != bg.r || bot.g != bg.g || bot.b != bg.b)
                p.cell(cx, cy, top, bot);
        }
    }
    return pen;
}

} // namespace chronos::font
