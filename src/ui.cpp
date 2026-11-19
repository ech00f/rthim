#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {
namespace {

float hash01(int i) {
    float v = std::sin(float(i) * 12.9898f + 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

Color contrastText(const Color &bg, const Color &preferred) {
    if (std::fabs(luminance(bg) - luminance(preferred)) < 0.3f) {
        return (luminance(bg) < 0.5f) ? hex(0xFFFFFF) : hex(0x141014);
    }
    return preferred;
}

} // namespace

void background(Gfx &g, const Style &st, float t, float beat) {
    g.gradientRect({0, 0, W, H}, st.bgTop, st.bgBottom);

    // Медленные пятна, чтобы фон не был плоским.
    for (int i = 0; i < 3; ++i) {
        float a = t * (0.05f + 0.02f * float(i)) + float(i) * 2.1f;
        float x = W * (0.2f + 0.6f * hash01(i * 7 + 3)) +
                  std::cos(a) * 60.f;
        float y = H * (0.15f + 0.7f * hash01(i * 5 + 9)) +
                  std::sin(a * 1.3f) * 40.f;
        g.glowCircle(x, y, 240.f + 40.f * float(i), st.accent,
                     0.03f + 0.02f * beat);
    }

    if (st.hardShadow > 4.f) {
        // Бумажный полутон.
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 16; ++x) {
                g.circle(40.f + float(x) * 80.f, 40.f + float(y) * 80.f,
                         1.2f, fade(st.dim, 0.10f));
            }
        }
    } else if (st.glow > 0.3f) {
        // Неоновые искры.
        for (int i = 0; i < 22; ++i) {
            float x = hash01(i * 11) * W;
            float y = hash01(i * 13 + 5) * H;
            float tw = 0.5f + 0.5f * std::sin(t * 1.7f + float(i));
            g.circle(x, y, 1.f + 1.6f * tw,
                     fade(st.accent2, 0.10f + 0.10f * tw));
        }
    }

    // Полоса-подложка под низ экрана: тексту удобнее.
    g.gradientRect({0, H - 120.f, W, 120.f}, fade(st.bgBottom, 0.f),
                   fade(st.bgBottom, 0.55f));
}

void panel(Gfx &g, const Style &st, Rect r, float radius, float alpha) {
    if (st.softShadow > 0.5f) {
        g.fillRoundRect({r.x + 4.f, r.y + 10.f, r.w, r.h}, radius, 0.f,
                        fade(hex(0x000000), 0.10f * alpha));
    }
    if (st.hardShadow > 2.f) {
        g.fillRoundRect({r.x + 7.f, r.y + 7.f, r.w, r.h}, radius, 0.f,
                        fade(hex(0x000000), 0.35f * alpha));
    }
    g.fillRoundRect(r, radius, 0.f, fade(st.btnFill, alpha));
    if (st.borderWidth > 0.2f) {
        g.fillRoundRect(r.inflated(1.f), radius + 1.f, 0.f,
                        fade(st.btnBorder, 0.30f * alpha));
        g.fillRoundRect(r, radius, 0.f, fade(st.btnFill, alpha));
    }
}

void button(Gfx &g, const Style &st, Rect r, const std::string &label,
            float hover, float press, bool primary, bool enabled) {
    float grow = 2.5f * hover - 2.f * press;
    Rect rr{r.x - grow, r.y - grow, r.w + 2.f * grow, r.h + 2.f * grow};
    float rad = st.radiusFactor * rr.h * 0.5f;

    Color fill = primary ? mix(st.accent, st.btnFill, 0.10f) : st.btnFill;
    if (!enabled) {
        fill = mix(st.btnFill, st.dim, 0.35f);
    } else if (hover > 0.01f) {
        fill = mix(fill, st.accent, (primary ? 0.12f : 0.10f) * hover);
    }

    if (st.hardShadow > 2.f) {
        float sh = st.hardShadow * 0.55f;
        g.fillRoundRect({rr.x + sh, rr.y + sh, rr.w, rr.h}, rad, st.shear,
                        fade(hex(0x000000), enabled ? 0.85f : 0.25f));
    }
    if (st.glow > 0.05f && hover > 0.01f) {
        g.glowCircle(rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f, rr.w * 0.55f,
                     primary ? st.accent : st.accent2,
                     0.12f * hover * st.glow);
    }

    g.fillRoundRect(rr, rad, st.shear, fill);
    if (st.borderWidth > 0.2f) {
        float bw = st.borderWidth;
        g.fillRoundRect(rr.inflated(bw), rad + bw, st.shear,
                        fade(primary ? st.accent : st.btnBorder, 0.60f));
        g.fillRoundRect(rr, rad, st.shear, fill);
    }

    Color txt = primary ? contrastText(fill, hex(0xFFFFFF)) : st.btnText;
    if (!primary) {
        txt = mix(st.btnText, st.btnTextHover, hover);
    }
    if (!enabled) {
        txt = fade(txt, 0.45f);
    }
    float th = g.textHeight(15, true);
    g.text(label, 15, txt, rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f - th * 0.5f,
           Gfx::Center, 2.4f + 1.2f * hover, true);
}

void chip(Gfx &g, const Style &st, Rect r, const std::string &text,
          bool accent) {
    Color c = accent ? st.accent : st.dim;
    g.fillRoundRect(r, r.h * 0.5f, 0.f, fade(c, accent ? 0.14f : 0.07f));
    g.fillRoundRect(r.inflated(1.f), r.h * 0.5f + 1.f, 0.f,
                    fade(c, accent ? 0.35f : 0.18f));
    g.fillRoundRect(r, r.h * 0.5f, 0.f, fade(st.bgTop, 0.75f));
    g.fillRoundRect(r, r.h * 0.5f, 0.f, fade(c, accent ? 0.14f : 0.07f));
    float th = g.textHeight(9, true);
    g.text(text, 9, fade(accent ? st.accent : st.text, accent ? 0.95f : 0.72f),
           r.x + r.w * 0.5f, r.y + r.h * 0.5f - th * 0.5f, Gfx::Center, 1.6f,
           true);
}

void toggle(Gfx &g, const Style &st, Rect r, bool on, float hover) {
    Color track = on ? st.accent : mix(st.btnFill, st.dim, 0.25f);
    g.fillRoundRect(r, r.h * 0.5f, 0.f, fade(track, on ? 0.85f : 0.30f));
    g.fillRoundRect(r.inflated(1.f), r.h * 0.5f + 1.f, 0.f,
                    fade(st.btnBorder, 0.30f));
    g.fillRoundRect(r, r.h * 0.5f, 0.f, fade(track, on ? 0.85f : 0.30f));

    float kr = r.h * 0.42f + 1.f * hover;
    float kx = on ? (r.x + r.w - r.h * 0.5f) : (r.x + r.h * 0.5f);
    g.circle(kx, r.y + r.h * 0.5f, kr,
             fade(on ? contrastText(st.accent, hex(0xFFFFFF)) : st.text, 0.92f),
             24);
}

void slider(Gfx &g, const Style &st, Rect r, float value, float hover,
            bool active) {
    value = clamp01(value);
    float cy = r.y + r.h * 0.5f;
    g.fillRoundRect({r.x, cy - 2.f, r.w, 4.f}, 2.f, 0.f,
                    fade(st.text, 0.12f + 0.06f * hover));
    g.fillRoundRect({r.x, cy - 2.f, r.w * value, 4.f}, 2.f, 0.f,
                    fade(st.accent, 0.85f));
    float kr = 8.f + 3.f * hover + (active ? 2.f : 0.f);
    float kx = r.x + r.w * value;
    g.glowCircle(kx, cy, kr * 2.6f, st.accent, 0.16f + 0.12f * hover);
    g.circle(kx, cy, kr, fade(st.accent, 0.95f), 28);
    g.circle(kx, cy, kr * 0.45f,
             fade(contrastText(st.accent, hex(0xFFFFFF)), 0.9f), 20);
}

void rowLabel(Gfx &g, const Style &st, const std::string &text,
              const std::string &hint, float x, float y, bool selected) {
    Color c = selected ? st.text : mix(st.text, st.dim, 0.25f);
    g.text(text, 15, fade(c, 0.95f), x, y, Gfx::Left, 1.2f, true);
    if (!hint.empty()) {
        g.text(hint, 9, fade(st.dim, 0.62f), x, y + 23.f, Gfx::Left, 1.2f,
               false);
    }
}

void arrows(Gfx &g, const Style &st, float cx, float cy, float hoverL,
            float hoverR, bool enabled) {
    for (int i = 0; i < 2; ++i) {
        float dir = (i == 0) ? -1.f : 1.f;
        float hov = (i == 0) ? hoverL : hoverR;
        Color cc = fade(enabled ? mix(st.dim, st.text, hov) : st.dim,
                        0.55f + 0.4f * hov);
        float x = cx + dir * 26.f;
        g.line(x - dir * 5.f, cy - 7.f, x + dir * 2.f, cy, 2.f, cc);
        g.line(x + dir * 2.f, cy, x - dir * 5.f, cy + 7.f, 2.f, cc);
    }
}

void title(Gfx &g, const Style &st, const std::string &text, float x, float y,
           float alpha) {
    g.text(text, 36, fade(st.text, 0.96f * alpha), x, y, Gfx::Left, 2.f, true);
    float w = g.textWidth(text, 36, 2.f, true);
    g.fillRect({x, y + 48.f, w, 2.f}, fade(st.accent, 0.55f * alpha));
}

void divider(Gfx &g, const Style &st, float x, float y, float w, float alpha) {
    g.fillRect({x, y, w, 1.f}, fade(st.dim, alpha));
}

std::string formatTime(double ms) {
    if (ms < 0) {
        ms = 0;
    }
    int total = int(ms / 1000.0 + 0.5);
    int m = total / 60;
    int s = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", m, s);
    return buf;
}

std::string formatCount(int n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d", n);
    return buf;
}

} // namespace ui