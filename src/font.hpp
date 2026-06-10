#pragma once
// chronos::font — a scalable, CRISP stroke font for the terminal.
//
// Why this is clean where the naive half-block approach was muddy:
//
//   A terminal cell holds ONE glyph + ONE foreground + ONE background colour.
//   If you draw a glyph by blending ink into the two halves of a `▀` cell, any
//   edge that cuts a half-pixel diagonally becomes a grey smear — there is no
//   way to say "this corner is ink, that corner is sky" with a single fg/bg.
//
//   The fix used by every good terminal-graphics renderer: rasterize the glyph
//   into a HIGH-RES binary coverage mask (here 2×2 sub-pixels per cell — the
//   quadrant grid), then for each cell pick the Unicode BLOCK glyph whose four
//   quadrants best match the coverage, painting fg = ink, bg = backdrop. Edges
//   become sharp two-tone block shapes, not anti-alias mush. Quadrant glyphs
//   (▘▝▖▗▀▄▌▐▚▞█ …) are universally supported, so it renders identically across
//   terminals.
//
//   We supersample each quadrant 3×3 and threshold at 50% coverage, which keeps
//   stems even and curves smooth. The SAME glyph definition renders crisp at any
//   pixel height — the strokes are vectors in a normalized em-box.

#include "gfx.hpp"
#include <array>
#include <cmath>
#include <vector>

namespace chronos::font {

using gfx::Col;
struct Pt { float x, y; };

struct Glyph {
    float adv = 0.6f;
    std::vector<std::vector<Pt>> strokes;
};

inline float seg_dist(float px, float py, Pt a, Pt b) {
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = px - a.x, wy = py - a.y;
    float vv = vx * vx + vy * vy;
    float t = vv > 1e-9f ? std::clamp((wx * vx + wy * vy) / vv, 0.f, 1.f) : 0.f;
    float dx = px - (a.x + t * vx), dy = py - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

// ── glyph table ─────────────────────────────────────────────────────────────
namespace tbl {

inline void arc(std::vector<Pt>& dst, float cx, float cy, float rx, float ry,
                float a0, float a1, int seg = 16) {
    for (int i = 0; i <= seg; ++i) {
        float t = a0 + (a1 - a0) * (float(i) / seg);
        dst.push_back({cx + rx * std::cos(t), cy - ry * std::sin(t)});
    }
}
constexpr float PI = 3.14159265f;

inline Glyph make(char32_t cp) {
    const float L = 0.18f, R = 0.82f, M = 0.5f;
    const float T = 0.10f, C = 0.5f,  B = 0.90f;
    const float rx = (R - L) / 2.f;
    const float qy = (C - T);
    Glyph g; g.adv = 0.72f;
    switch (cp) {
    case U'0': {
        std::vector<Pt> o;
        arc(o, M, C, rx, qy, PI/2, PI/2 + 2*PI, 32);
        g.strokes = { o };                               // clean ellipse, no slash
        break; }
    case U'1':
        g.adv = 0.48f;
        g.strokes = { { {L+0.02f,T+0.18f},{M,T},{M,B} }, { {L-0.04f,B},{R-0.08f,B} } };
        break;
    case U'2': {
        std::vector<Pt> s; arc(s, M, T+qy*0.58f, rx, qy*0.58f, PI*0.95f, -PI*0.28f, 18);
        s.push_back({L, B}); s.push_back({R, B});
        g.strokes = { s };
        break; }
    case U'3': {
        std::vector<Pt> s;
        arc(s, M, T+qy*0.52f, rx, qy*0.52f, PI*0.98f, -PI*0.5f, 18);
        arc(s, M, B-qy*0.52f, rx, qy*0.52f, PI*0.5f, -PI*0.98f, 18);
        g.strokes = { s };
        break; }
    case U'4':
        g.strokes = { { {R-0.10f,B},{R-0.10f,T} },
                      { {R-0.10f,T},{L-0.04f,C+0.16f},{R+0.04f,C+0.16f} } };
        break;
    case U'5': {
        std::vector<Pt> s = { {R-0.02f,T},{L,T},{L,C-0.04f} };
        arc(s, M, C+qy*0.42f, rx, qy*0.58f, PI*0.6f, -PI*0.98f, 20);
        g.strokes = { s };
        break; }
    case U'6': {
        std::vector<Pt> o; arc(o, M, B-qy*0.55f, rx, qy*0.55f, 0, 2*PI, 28);
        std::vector<Pt> tail; arc(tail, M, C+0.02f, rx, qy*1.12f, PI*0.5f, PI*1.04f, 16);
        g.strokes = { o, tail };
        break; }
    case U'7':
        g.strokes = { { {L,T},{R,T},{M-0.06f,B} } };
        break;
    case U'8': {
        std::vector<Pt> top; arc(top, M, T+qy*0.5f, rx*0.84f, qy*0.5f, 0, 2*PI, 26);
        std::vector<Pt> bot; arc(bot, M, B-qy*0.52f, rx, qy*0.52f, 0, 2*PI, 28);
        g.strokes = { top, bot };
        break; }
    case U'9': {
        std::vector<Pt> o; arc(o, M, T+qy*0.55f, rx, qy*0.55f, 0, 2*PI, 28);
        std::vector<Pt> tail; arc(tail, M, C-0.02f, rx, qy*1.12f, -PI*0.5f, PI*0.04f, 16);
        g.strokes = { o, tail };
        break; }
    case U':':
        g.adv = 0.30f;
        g.strokes = { { {M,T+qy*0.65f},{M,T+qy*0.65f+0.001f} },
                      { {M,B-qy*0.65f},{M,B-qy*0.65f+0.001f} } };
        break;
    case U'.':
        g.adv = 0.28f;
        g.strokes = { { {M,B-0.04f},{M,B-0.039f} } };
        break;
    case U'-':
        g.adv = 0.5f;
        g.strokes = { { {L,C},{R,C} } };
        break;
    case U' ': g.adv = 0.40f; break;
    default:   g.adv = 0.40f; break;
    }
    return g;
}

} // namespace tbl

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

inline float measure_em(std::string_view s) {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        char32_t cp; i += gfx::utf8_decode(s, i, cp);
        w += glyph(cp).adv + 0.08f;
    }
    return w > 0 ? w - 0.08f : 0;
}

// ── octant block table: a cell is 2 wide × 4 tall sub-pixels (Unicode 16 “Block
// Octant” glyphs, U+1CD00..). bit layout (LSB first):
//   bit0=r0c0 bit1=r0c1   bit2=r1c0 bit3=r1c1
//   bit4=r2c0 bit5=r2c1   bit6=r3c0 bit7=r3c1
// 8 sub-pixels per cell → 4× the vertical resolution of the half-block, so
// strokes and curves resolve genuinely crisp. The 6 half/quadrant-equivalent
// masks reuse the legacy block elements; the 20 masks with no octant fall back
// to the nearest quadrant glyph.
inline char32_t octant_glyph(int mask) {
    static const char32_t OCT[256] = {
        0x20, 0x2598, 0x259D, 0x2580, 0x1CD00, 0x2598, 0x1CD01, 0x1CD02,
        0x1CD03, 0x1CD04, 0x259D, 0x1CD05, 0x1CD06, 0x1CD07, 0x1CD08, 0x2580,
        0x1CD09, 0x1CD0A, 0x1CD0B, 0x1CD0C, 0x258C, 0x1CD0D, 0x1CD0E, 0x1CD0F,
        0x1CD10, 0x1CD11, 0x1CD12, 0x1CD13, 0x1CD14, 0x1CD15, 0x1CD16, 0x1CD17,
        0x1CD18, 0x1CD19, 0x1CD1A, 0x1CD1B, 0x1CD1C, 0x1CD1D, 0x1CD1E, 0x1CD1F,
        0x2590, 0x1CD20, 0x1CD21, 0x1CD22, 0x1CD23, 0x1CD24, 0x1CD25, 0x1CD26,
        0x1CD27, 0x1CD28, 0x1CD29, 0x1CD2A, 0x1CD2B, 0x1CD2C, 0x1CD2D, 0x1CD2E,
        0x1CD2F, 0x1CD30, 0x1CD31, 0x1CD32, 0x1CD33, 0x1CD34, 0x1CD35, 0x2588,
        0x2596, 0x1CD36, 0x1CD37, 0x1CD38, 0x1CD39, 0x1CD3A, 0x1CD3B, 0x1CD3C,
        0x1CD3D, 0x1CD3E, 0x1CD3F, 0x1CD40, 0x1CD41, 0x1CD42, 0x1CD43, 0x1CD44,
        0x2596, 0x1CD45, 0x1CD46, 0x1CD47, 0x1CD48, 0x258C, 0x1CD49, 0x1CD4A,
        0x1CD4B, 0x1CD4C, 0x259E, 0x1CD4D, 0x1CD4E, 0x1CD4F, 0x1CD50, 0x259B,
        0x1CD51, 0x1CD52, 0x1CD53, 0x1CD54, 0x1CD55, 0x1CD56, 0x1CD57, 0x1CD58,
        0x1CD59, 0x1CD5A, 0x1CD5B, 0x1CD5C, 0x1CD5D, 0x1CD5E, 0x1CD5F, 0x1CD60,
        0x1CD61, 0x1CD62, 0x1CD63, 0x1CD64, 0x1CD65, 0x1CD66, 0x1CD67, 0x1CD68,
        0x1CD69, 0x1CD6A, 0x1CD6B, 0x1CD6C, 0x1CD6D, 0x1CD6E, 0x1CD6F, 0x1CD70,
        0x2597, 0x1CD71, 0x1CD72, 0x1CD73, 0x1CD74, 0x1CD75, 0x1CD76, 0x1CD77,
        0x1CD78, 0x1CD79, 0x1CD7A, 0x1CD7B, 0x1CD7C, 0x1CD7D, 0x1CD7E, 0x1CD7F,
        0x1CD80, 0x1CD81, 0x1CD82, 0x1CD83, 0x1CD84, 0x1CD85, 0x1CD86, 0x1CD87,
        0x1CD88, 0x1CD89, 0x1CD8A, 0x1CD8B, 0x1CD8C, 0x1CD8D, 0x1CD8E, 0x1CD8F,
        0x2597, 0x1CD90, 0x1CD91, 0x1CD92, 0x1CD93, 0x259A, 0x1CD94, 0x1CD95,
        0x1CD96, 0x1CD97, 0x2590, 0x1CD98, 0x1CD99, 0x1CD9A, 0x1CD9B, 0x259C,
        0x1CD9C, 0x1CD9D, 0x1CD9E, 0x1CD9F, 0x1CDA0, 0x1CDA1, 0x1CDA2, 0x1CDA3,
        0x1CDA4, 0x1CDA5, 0x1CDA6, 0x1CDA7, 0x1CDA8, 0x1CDA9, 0x1CDAA, 0x1CDAB,
        0x2584, 0x1CDAC, 0x1CDAD, 0x1CDAE, 0x1CDAF, 0x1CDB0, 0x1CDB1, 0x1CDB2,
        0x1CDB3, 0x1CDB4, 0x1CDB5, 0x1CDB6, 0x1CDB7, 0x1CDB8, 0x1CDB9, 0x1CDBA,
        0x1CDBB, 0x1CDBC, 0x1CDBD, 0x1CDBE, 0x1CDBF, 0x1CDC0, 0x1CDC1, 0x1CDC2,
        0x1CDC3, 0x1CDC4, 0x1CDC5, 0x1CDC6, 0x1CDC7, 0x1CDC8, 0x1CDC9, 0x1CDCA,
        0x1CDCB, 0x1CDCC, 0x1CDCD, 0x1CDCE, 0x1CDCF, 0x1CDD0, 0x1CDD1, 0x1CDD2,
        0x1CDD3, 0x1CDD4, 0x1CDD5, 0x1CDD6, 0x1CDD7, 0x1CDD8, 0x1CDD9, 0x1CDDA,
        0x2584, 0x1CDDB, 0x1CDDC, 0x1CDDD, 0x1CDDE, 0x2599, 0x1CDDF, 0x1CDE0,
        0x1CDE1, 0x1CDE2, 0x259F, 0x1CDE3, 0x2588, 0x1CDE4, 0x1CDE5, 0x2588,
    };
    return OCT[mask & 255];
}

// ════════════════════════════════════════════════════════════════════════
//  draw_text — crisp block-glyph rasterization at any pixel height.
//
//  px,py     : top-left in CELL coords (x = column, y = row).
//  height_px : em-box height measured in OCTANT rows (= 4 × cell rows).
//  weight    : stroke thickness as fraction of height.
//  fg        : ink;  bg_fn(col,row) -> backdrop colour.
//  Returns advanced x in fractional cells.
// ═════════════════════════════════════════════════════════════════════════════
template <class BgFn>
inline float draw_text(gfx::Painter& p, float px, float py, float height_px,
                       std::string_view s, Col fg, BgFn&& bg_fn,
                       float weight = 0.13f) {
    // Octant grid: 2 sub-pixels per cell wide, 4 tall. Sub-pixels are square
    // (cell ~2:1), so we work in a single square sub-pixel space: x has 2 units
    // per cell, y has 4 units per cell. `em` is the glyph height in sub-y units.
    const float em   = height_px;                  // octant-rows per em
    const float SX   = 2.f;                          // sub-pixels per cell, x
    const float SY   = 4.f;                          // sub-pixels per cell, y
    const float half = std::max(0.85f, weight * em * 0.5f);

    struct Placed { const Glyph* g; float ox; };    // ox in sub-x units
    std::vector<Placed> placed;
    float pen = px * SX;                             // pen in sub-x units
    for (size_t i = 0; i < s.size();) {
        char32_t cp; i += gfx::utf8_decode(s, i, cp);
        const Glyph& g = glyph(cp);
        placed.push_back({&g, pen});
        pen += (g.adv + 0.08f) * em;                 // advance (square sub-units)
    }
    float sx0 = px * SX,  sx1 = pen;
    float sy0 = py * SY,  sy1 = py * SY + em;

    int cx0 = std::max(0, int(std::floor(sx0 / SX)) - 1);
    int cx1 = std::min(p.cols() - 1, int(std::ceil(sx1 / SX)) + 1);
    int cy0 = std::max(0, int(std::floor(sy0 / SY)) - 1);
    int cy1 = std::min(p.rows() - 1, int(std::ceil(sy1 / SY)) + 1);

    // signed-distance test of a sub-pixel-space point against all strokes.
    auto inked = [&](float sx, float sy) -> bool {
        float best = 1e9f;
        for (const Placed& pl : placed) {
            float gx = (sx - pl.ox) / em;            // into normalized em box
            float gy = (sy - sy0) / em;
            if (gx < -0.3f || gx > pl.g->adv + 0.3f || gy < -0.3f || gy > 1.3f)
                continue;
            for (const auto& st : pl.g->strokes) {
                if (st.size() == 1) {
                    float dx = (gx - st[0].x) * em, dy = (gy - st[0].y) * em;
                    best = std::min(best, std::sqrt(dx*dx + dy*dy));
                    continue;
                }
                for (size_t k = 0; k + 1 < st.size(); ++k)
                    best = std::min(best, seg_dist(gx, gy, st[k], st[k+1]) * em);
            }
        }
        return best <= half;
    };

    // one octant sub-pixel: 3×3 supersample, majority vote → crisp but smooth.
    auto sub_on = [&](float sx, float sy) -> bool {
        int hit = 0;
        for (int sj = 0; sj < 3; ++sj)
            for (int si = 0; si < 3; ++si)
                if (inked(sx + (si + 0.5f) / 3.f, sy + (sj + 0.5f) / 3.f)) ++hit;
        return hit >= 5;
    };

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            float bx = cx * SX, by = cy * SY;
            // 8 octant sub-pixels: bit = r*2 + c  (r 0..3 top→bottom, c 0..1)
            int mask = 0;
            for (int r = 0; r < 4; ++r)
                for (int col = 0; col < 2; ++col)
                    if (sub_on(bx + col, by + r)) mask |= 1 << (r * 2 + col);
            if (mask == 0) continue;
            Col bg = bg_fn(cx, cy);
            p.glyph_cell(cx, cy, octant_glyph(mask), fg, bg);
        }
    }
    return pen / SX;
}

} // namespace chronos::font
