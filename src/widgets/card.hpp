#pragma once
// chronos::ui::card — shared chrome for the dashboard cards (sun/moon/weather).
//
// Mirrors the calendar rail's section style — solid accent chip banners with
// near-black ink, gradient hairlines, right-aligned values — so the whole UI
// reads as one design. Everything composites over the frosted-glass fill via
// bg_at() instead of stamping solid panel_bg boxes behind the glyphs.

#include "../widget.hpp"
#include <format>

namespace chronos::ui::card {

// the frosted fill behind a cell (fallback: dark glass)
inline Col frostbg(Painter& p, int x, int y) {
    return p.bg_at(x, y, Col{0.05f, 0.055f, 0.09f});
}

// text composited over the glass — no solid box behind the glyphs
inline void txt(Painter& p, int x, int y, std::string_view s, Col fg, bool bold = false) {
    p.text_over(x, y, s, fg, [&p](int cx, int cy) { return frostbg(p, cx, cy); }, bold);
}

// truncate to `cols` display columns, adding an ellipsis if cut
inline std::string clip_cols(const std::string& s, int cols) {
    if (cols <= 0) return "";
    if ((int)gfx::utf8_cols(s) <= cols) return s;
    if (cols == 1) return "\u2026";
    std::string out; int w = 0;
    for (size_t i = 0; i < s.size();) {
        char32_t cp; int n = gfx::utf8_decode(s, i, cp);
        int cw = (cp >= 0x1100) ? 2 : 1;
        if (w + cw > cols - 1) break;
        out.append(s, i, n); w += cw; i += n;
    }
    out += "\u2026";
    return out;
}

// a label / value row: key dim on the left, value bold right-aligned to rx
inline void kv(Painter& p, int x, int rx, int y, std::string_view k,
               const std::string& v, Col kc, Col vc) {
    txt(p, x, y, k, kc);
    txt(p, rx - (int)gfx::utf8_cols(v) + 1, y, v, vc, true);
}

// slim progress gauge: bright fill in a recessed dark slot (no ░ speckle).
// Boundary cell uses the eighth blocks for smooth sub-cell fill.
inline void gauge(Painter& p, int x, int y, int w, float v, Col fill) {
    v = gfx::clampf(v, 0.f, 1.f);
    static const char* eighths[] = {"", "\u258f","\u258e","\u258d","\u258c",
                                    "\u258b","\u258a","\u2589","\u2588"};
    float total = v * w;
    for (int i = 0; i < w; ++i) {
        Col slot = gfx::scale(frostbg(p, x + i, y), 0.55f);   // recessed slot
        float f = gfx::clampf(total - i, 0.f, 1.f);
        if (f >= 1.f)      p.text(x + i, y, "\u2588", fill, slot);
        else if (f <= 0.f) p.text(x + i, y, " ", slot, slot);
        else {
            int e = std::clamp((int)std::round(f * 8.f), 1, 8);
            p.text(x + i, y, eighths[e], fill, slot);
        }
    }
}

// title row: solid accent chip with letter-spaced label + glow cell + gradient
// hairline, and an optional tinted-glass badge chip right-aligned. The same
// banner language as the calendar rail's section headers.
inline void title(Painter& p, const Rect& in, const char* label, Col accent,
                  const std::string& badge = "", Col badge_col = {1, 1, 1}) {
    int y  = in.y;
    int x0 = in.x + 1;
    int xe = in.right() - 1;                       // exclusive right edge
    int badge_w = badge.empty() ? 0 : (int)gfx::utf8_cols(badge) + 2;
    int bx = xe - badge_w;

    // letter-space the label when roomy; fall back to plain, then clip
    std::string plain = label, spaced;
    for (const char* s = label; *s; ++s) { if (s != label) spaced += ' '; spaced += *s; }
    int avail = bx - x0 - 4;                       // pads + glow + 1 hairline
    std::string lbl = ((int)gfx::utf8_cols(spaced) <= avail) ? spaced
                    : ((int)gfx::utf8_cols(plain)  <= avail) ? plain
                    : clip_cols(plain, std::max(1, avail));
    int lw = (int)gfx::utf8_cols(lbl);

    Col ink{0.04f, 0.04f, 0.06f};                  // pure near-black label ink
    int cx1 = std::min(xe, x0 + lw + 2);
    for (int cx = x0; cx < cx1; ++cx) p.text(cx, y, " ", ink, accent);
    p.text(x0 + 1, y, lbl, ink, accent, true);
    if (cx1 < xe) p.text(cx1, y, " ", ink, gfx::scale(accent, 0.45f));

    // gradient hairline easing the accent into the glass
    int hx0 = cx1 + 1, hx1 = bx - (badge_w ? 1 : 0);
    int span = std::max(1, hx1 - hx0);
    for (int cx = hx0; cx < hx1; ++cx) {
        float t = float(cx - hx0) / float(span);
        Col bgc = frostbg(p, cx, y);
        p.text(cx, y, "\u2500", gfx::mix(gfx::scale(accent, 0.55f), bgc, t * t), bgc);
    }

    // badge: a quiet tinted-glass chip (bright text, faint colour wash)
    if (badge_w && bx > cx1 + 1) {
        Col bbg = gfx::mix(frostbg(p, bx, y), badge_col, 0.20f);
        for (int cx = bx; cx < xe; ++cx) p.text(cx, y, " ", badge_col, bbg);
        p.text(bx + 1, y, badge, gfx::mix(badge_col, Col{1, 1, 1}, 0.15f), bbg, true);
    }
}

} // namespace chronos::ui::card
