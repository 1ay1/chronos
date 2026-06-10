#pragma once
// chronos::gfx — colour math + a Painter that wraps maya's half-block Canvas.
//
// Two RGB sub-pixels per terminal cell (▀: fg=top, bg=bottom). The Painter
// owns the per-frame style cache and exposes high-level graphical primitives
// (filled rects, discs, gauges, sparklines, rounded panels, glow text) that
// every widget draws with — so widgets never touch the raw Canvas API.

#include <maya/internal.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace chronos::gfx {

// ── colour ────────────────────────────────────────────────────────────────
struct Col { float r = 0, g = 0, b = 0; };

inline Col mix(Col a, Col b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline Col add(Col a, Col b)        { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline Col mul(Col a, Col b)        { return {a.r * b.r, a.g * b.g, a.b * b.b}; }
inline Col scale(Col a, float s)    { return {a.r * s, a.g * s, a.b * s}; }
inline Col hex(uint32_t h) {
    return {((h >> 16) & 0xFF) / 255.f, ((h >> 8) & 0xFF) / 255.f, (h & 0xFF) / 255.f};
}
inline float luma(Col c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

// Posterize: snap each channel to one of `steps` discrete levels. Flattens a
// smooth gradient into crisp flat bands — the blocky, hard-edged look that
// reads cleanly (think Minecraft / pixel art) instead of mushy interpolation.
inline Col posterize(Col c, float steps) {
    auto q = [steps](float v) {
        return std::round(std::clamp(v, 0.f, 1.f) * (steps - 1.f)) / (steps - 1.f);
    };
    return {q(c.r), q(c.g), q(c.b)};
}

// Saturate: push colour away from its grey luma by `s` (1 = unchanged, >1 more
// vivid). Pixel-art / Minecraft palettes are punchy, not desaturated.
inline Col saturate(Col c, float s) {
    float l = luma(c);
    return {std::clamp(l + (c.r - l) * s, 0.f, 1.f),
            std::clamp(l + (c.g - l) * s, 0.f, 1.f),
            std::clamp(l + (c.b - l) * s, 0.f, 1.f)};
}

inline float smoothstep(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0 + 1e-9f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline float clampf(float x, float lo, float hi) { return std::clamp(x, lo, hi); }

// ── value-noise / fbm (clouds, texture) ─────────────────────────────────────
inline float frac(float x) { return x - std::floor(x); }
inline float hash2(float x, float y) {
    return frac(std::sin(x * 127.1f + y * 311.7f) * 43758.5453f);
}
inline float vnoise(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;
    float a = hash2(ix, iy),     b = hash2(ix + 1, iy);
    float c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    return (a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy;
}
inline float fbm(float x, float y) {
    float v = 0, amp = 0.5f;
    for (int i = 0; i < 4; ++i) { v += vnoise(x, y) * amp; x *= 2.03f; y *= 2.01f; amp *= 0.5f; }
    return v;
}

// ── UTF-8 → codepoint (for canvas.set, which takes char32_t) ────────────────
inline int utf8_decode(std::string_view s, size_t i, char32_t& cp) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80)      { cp = c; return 1; }
    if (c < 0xE0)      { cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); return 2; }
    if (c < 0xF0)      { cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); return 3; }
    cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F);
    return 4;
}
// number of *display columns* a UTF-8 string occupies (treats emoji/wide cps as 2)
inline int utf8_cols(std::string_view s) {
    int cols = 0;
    for (size_t i = 0; i < s.size();) {
        char32_t cp; i += utf8_decode(s, i, cp);
        bool wide = (cp >= 0x1100 && (cp <= 0x115F || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                     (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                     (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
                     (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) ||
                     (cp >= 0x2600 && cp <= 0x27BF)));
        cols += wide ? 2 : 1;
    }
    return cols;
}

// ════════════════════════════════════════════════════════════════════════
//  Painter — the one thing widgets draw with
// ════════════════════════════════════════════════════════════════════════
class Painter {
public:
    Painter(maya::Canvas& cv, maya::StylePool& pool, int w, int h)
        : cv_(cv), pool_(pool), w_(w), h_(h) {}

    [[nodiscard]] int cols()     const { return w_; }      // terminal columns
    [[nodiscard]] int rows()     const { return h_; }      // terminal rows
    [[nodiscard]] int pix_w()    const { return w_; }      // sub-pixel width
    [[nodiscard]] int pix_h()    const { return h_ * 2; }  // sub-pixel height

    // -- style interning (quantized so the pool stays small) -----------------
    uint16_t cell_style(Col top, Col bot) {
        return pool_.intern(maya::Style{}
            .with_fg(maya::Color::rgb(q(top.r), q(top.g), q(top.b)))
            .with_bg(maya::Color::rgb(q(bot.r), q(bot.g), q(bot.b))));
    }
    uint16_t text_style(Col fg, Col bg, bool bold = false) {
        auto s = maya::Style{}
            .with_fg(maya::Color::rgb(q(fg.r), q(fg.g), q(fg.b)))
            .with_bg(maya::Color::rgb(q(bg.r), q(bg.g), q(bg.b)));
        if (bold) s = s.with_bold();
        return pool_.intern(s);
    }

    // -- sub-pixel plot: write one of the two pixels in a cell ---------------
    // Reads the partner pixel from a shadow buffer so stacking a top over a
    // previously-set bottom keeps both. For widgets we mostly use cell()/rect.
    void cell(int cx, int cy, Col top, Col bot) {
        cv_.set(cx, cy, U'\u2580', cell_style(top, bot));
    }

    // full-resolution filled rect in *cell* space, uniform colour
    void fill_cells(int x, int y, int w, int h, Col c) {
        uint16_t st = cell_style(c, c);
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                cv_.set(xx, yy, U'\u2580', st);
    }

    // a single solid character cell (space) used for text backgrounds
    void blank_cell(int cx, int cy, Col bg) {
        cv_.set(cx, cy, U' ', text_style(bg, bg));
    }

    // an arbitrary glyph with crisp fg ink over a bg (used by the vector font's
    // quadrant block renderer). fg is the glyph colour, bg the backdrop.
    void glyph_cell(int cx, int cy, char32_t glyph, Col fg, Col bg) {
        cv_.set(cx, cy, glyph, text_style(fg, bg));
    }

    // -- text -----------------------------------------------------------------
    // Draw text with explicit fg/bg per cell.
    void text(int cx, int cy, std::string_view s, Col fg, Col bg, bool bold = false) {
        uint16_t st = text_style(fg, bg, bold);
        int x = cx;
        for (size_t i = 0; i < s.size();) {
            char32_t cp; i += utf8_decode(s, i, cp);
            cv_.set(x, cy, cp, st);
            x++;
        }
    }
    // Draw text whose background is sampled per-cell from a lambda (so text
    // floats over a gradient). bg_fn(cx,cy) -> Col.
    template <class F>
    void text_over(int cx, int cy, std::string_view s, Col fg, F&& bg_fn, bool bold = false) {
        int x = cx;
        for (size_t i = 0; i < s.size();) {
            char32_t cp; i += utf8_decode(s, i, cp);
            Col bg = bg_fn(x, cy);
            cv_.set(x, cy, cp, text_style(fg, bg, bold));
            x++;
        }
    }

    // -- rounded panel (frosted card) ----------------------------------------
    void panel(int x, int y, int w, int h, Col bg, Col border) {
        fill_cells(x, y, w, h, bg);
        uint16_t bs = text_style(border, bg);
        for (int xx = x + 1; xx < x + w - 1; ++xx) {
            cv_.set(xx, y, U'\u2500', bs);
            cv_.set(xx, y + h - 1, U'\u2500', bs);
        }
        for (int yy = y + 1; yy < y + h - 1; ++yy) {
            cv_.set(x, yy, U'\u2502', bs);
            cv_.set(x + w - 1, yy, U'\u2502', bs);
        }
        cv_.set(x, y, U'\u256d', bs);              cv_.set(x + w - 1, y, U'\u256e', bs);
        cv_.set(x, y + h - 1, U'\u2570', bs);      cv_.set(x + w - 1, y + h - 1, U'\u256f', bs);
    }

    // -- horizontal gauge bar (block fill) -----------------------------------
    // value 0..1 over `w` cells. Uses ▏▎▍▌▋▊▉█ sub-cell fill for smoothness.
    void gauge(int x, int y, int w, float value, Col fill, Col track, Col bg) {
        value = clampf(value, 0.f, 1.f);
        static const char* eighths[] = {"", "\u258f","\u258e","\u258d","\u258c",
                                        "\u258b","\u258a","\u2589","\u2588"};
        float total = value * w;
        for (int i = 0; i < w; ++i) {
            float cellfill = clampf(total - i, 0.f, 1.f);
            if (cellfill >= 1.f) {
                cv_.set(x + i, y, U'\u2588', text_style(fill, bg));
            } else if (cellfill <= 0.f) {
                cv_.set(x + i, y, U'\u2591', text_style(track, bg));  // light shade track
            } else {
                int e = (int)std::round(cellfill * 8.f);
                char32_t cp; utf8_decode(eighths[std::clamp(e,1,8)], 0, cp);
                cv_.set(x + i, y, cp, text_style(fill, bg));
            }
        }
    }

    // -- sparkline (8-level block bars) --------------------------------------
    template <class Vec>
    void sparkline(int x, int y, const Vec& vals, Col c, Col bg) {
        static const char* bars[] = {" ","\u2581","\u2582","\u2583","\u2584",
                                     "\u2585","\u2586","\u2587","\u2588"};
        float mn = 1e9f, mx = -1e9f;
        for (float v : vals) { mn = std::min(mn, v); mx = std::max(mx, v); }
        float span = (mx - mn) < 1e-6f ? 1.f : (mx - mn);
        int i = 0;
        for (float v : vals) {
            int lvl = std::clamp(int((v - mn) / span * 8.f), 0, 8);
            char32_t cp; utf8_decode(bars[lvl], 0, cp);
            cv_.set(x + i, y, cp, text_style(c, bg));
            i++;
        }
    }

private:
    static uint8_t q(float v) {                 // quantize to 32 levels/channel
        constexpr int Q = 32;
        int qi = std::clamp(int(v * (Q - 1) + 0.5f), 0, Q - 1);
        return uint8_t(qi * 255 / (Q - 1));
    }
    maya::Canvas&    cv_;
    maya::StylePool& pool_;
    int w_, h_;
};

} // namespace chronos::gfx
