#pragma once
// chronos::ui::StatusBarWidget — bottom keybar with highlighted keys.

#include "../widget.hpp"

namespace chronos::ui {

class StatusBarWidget : public Widget {
public:
    const char* name() const override { return "statusbar"; }

    void paint(Painter& p, const Rect& r, const Ctx& c) override {
        Col bg = c.theme.scrim;
        Col keyc = c.theme.accent, desc = c.theme.text_dim;
        p.fill_cells(r.x, r.y, r.w, 1, bg);

        struct KV { const char* k; const char* d; };
        static const KV items[] = {
            {"a","clock"}, {"c","calendar"}, {"w","clocks"},
            {"+/-","warp"}, {"0","now"}, {"q","quit"},
        };
        int x = r.x + 1;
        for (auto& it : items) {
            p.text(x, r.y, it.k, keyc, bg, true); x += (int)gfx::utf8_cols(it.k) + 1;
            p.text(x, r.y, it.d, desc, bg);        x += (int)gfx::utf8_cols(it.d) + 3;
            if (x >= r.right() - 2) break;
        }
    }
};

} // namespace chronos::ui
