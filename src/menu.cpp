#include "menu.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace menu {
namespace {

constexpr float W = 1280.f;
constexpr float H = 720.f;
constexpr int kCount = 4;
constexpr float kPi = 3.14159265358979f;

const char *kLabels[kCount] = {"PLAY", "TRACKS", "SETTINGS", "QUIT"};
const char *kNums[kCount] = {"01", "02", "03", "04"};
const char *kNames[3] = {"NEON", "BLOOM", "INK"};

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

Rect rectFor(int i) {
    constexpr float bw = 460.f;
    constexpr float bh = 62.f;
    constexpr float x = (W - bw) * 0.5f;
    constexpr float y0 = 296.f;
    constexpr float gap = 16.f;
    return {x, y0 + float(i) * (bh + gap), bw, bh};
}

// Детерминированный «шум» для декоративных частиц.
float hash01(int i) {
    float v = std::sin(float(i) * 12.9898f + 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

// Курсор -> координата стиля. У каждого мира есть «плато», а переход
// занимает узкую полосу: так стили остаются чистыми почти везде.
float styleCoordinate(float x01) {
    constexpr float a0 = 0.22f, a1 = 0.42f; // NEON  -> BLOOM
    constexpr float b0 = 0.58f, b1 = 0.78f; // BLOOM -> INK
    if (x01 <= a0) {
        return 0.f;
    }
    if (x01 < a1) {
        return smoothstep01((x01 - a0) / (a1 - a0));
    }
    if (x01 <= b0) {
        return 1.f;
    }
    if (x01 < b1) {
        return 1.f + smoothstep01((x01 - b0) / (b1 - b0));
    }
    return 2.f;
}

void backdrop(Gfx &g, const Style &st, const float w[3], float time,
              float cx, float cy) {
    g.gradientRect({0.f, 0.f, W, H}, st.bgTop, st.bgBottom);

    // На стыке двух миров добавляем акцентное свечение за меню,
    // чтобы переход читался как «сумерки», а не как серое пятно.
    float peak = 4.f * w[0] * w[1] + 4.f * w[1] * w[2];
    if (peak > 0.001f) {
        g.glowCircle(640.f, 330.f, 560.f, st.accent, 0.10f * peak);
    }

    float px = clamp01(cx / W) - 0.5f;
    float py = clamp01(cy / H) - 0.5f;

    // ---- NEON: перспективная сетка, свечение, скобки по углам ------------
    if (w[0] > 0.01f) {
        float a = w[0];
        Pt vp{640.f + px * 60.f, 430.f + py * 40.f};
        Color grid = fade(st.accent2, 0.16f * a);

        for (int i = -9; i <= 9; ++i) {
            g.line(vp.x, vp.y, 640.f + float(i) * 190.f, 790.f, 1.f, grid);
        }
        for (int k = 0; k < 12; ++k) {
            float f = std::fmod(time * 0.10f + float(k) / 12.f, 1.f);
            float y = vp.y + (H - vp.y + 60.f) * f * f;
            float al = 0.16f * a * (1.f - f * 0.55f);
            float halfSpan = 640.f * (0.35f + f);
            g.line(640.f - halfSpan, y, 640.f + halfSpan, y, 1.f,
                   fade(st.accent2, al));
        }

        g.glowCircle(150.f + px * -90.f, 170.f + py * -60.f, 360.f, st.accent,
                     0.20f * a);
        g.glowCircle(1140.f + px * -90.f, 620.f + py * -50.f, 320.f,
                     st.accent2, 0.14f * a);

        Color bracket = fade(st.accent, 0.55f * a);
        auto corner = [&](float x, float y, float dx, float dy) {
            g.line(x, y, x + dx * 28.f, y, 2.f, bracket);
            g.line(x, y, x, y + dy * 28.f, 2.f, bracket);
        };
        corner(20.f, 20.f, 1.f, 1.f);
        corner(W - 20.f, 20.f, -1.f, 1.f);
        corner(20.f, H - 20.f, 1.f, -1.f);
        corner(W - 20.f, H - 20.f, -1.f, -1.f);
    }

    // ---- BLOOM: пастельные пятна, кольца, плавающие точки ----------------
    if (w[1] > 0.01f) {
        float a = w[1];
        g.glowCircle(230.f + px * -70.f, 650.f + py * -40.f, 420.f,
                     st.accent, 0.18f * a);
        g.glowCircle(1060.f + px * -80.f, 130.f + py * -40.f, 460.f,
                     st.accent2, 0.18f * a);
        g.glowCircle(720.f, 340.f, 260.f, hex(0xFFFFFF), 0.12f * a);

        g.ring(1050.f, 560.f, 190.f, 1.5f, fade(st.accent, 0.18f * a));
        g.ring(1050.f, 560.f, 240.f, 1.f, fade(st.accent, 0.08f * a));

        for (int i = 0; i < 26; ++i) {
            float x = std::fmod(hash01(i) * W + time * (6.f + hash01(i + 40) * 10.f), W);
            float y = std::fmod(hash01(i + 90) * H + time * (9.f + hash01(i + 7) * 12.f), H);
            float r = 1.5f + hash01(i + 13) * 3.5f;
            g.circle(x, y, r, fade(st.accent, 0.14f * a), 10);
        }
    }

    // ---- INK: полутон, диагонали, рамка, красное кольцо ------------------
    if (w[2] > 0.01f) {
        float a = w[2];
        for (float y = 16.f; y < H; y += 34.f) {
            for (float x = 16.f; x < W; x += 34.f) {
                float r = 1.1f + 1.7f * (y / H);
                g.circle(x + std::fmod(y, 34.f) * 0.5f, y, r,
                         fade(st.text, 0.07f * a), 6);
            }
        }

        for (int i = 0; i < 7; ++i) {
            float x = 860.f + float(i) * 46.f;
            g.line(x, -20.f, x - 190.f, 210.f, 22.f, fade(st.text, 0.04f * a));
        }

        g.ring(1090.f, 552.f, 150.f, 2.f, fade(st.accent2, 0.55f * a));
        g.circle(1090.f - 106.f, 552.f - 106.f, 9.f, fade(st.accent2, 0.9f * a));

        Color frame = fade(st.text, 0.45f * a);
        g.line(16.f, 16.f, W - 16.f, 16.f, 1.2f, frame);
        g.line(16.f, H - 16.f, W - 16.f, H - 16.f, 1.2f, frame);
        g.line(16.f, 16.f, 16.f, H - 16.f, 1.2f, frame);
        g.line(W - 16.f, 16.f, W - 16.f, H - 16.f, 1.2f, frame);
    }
}

void titleBlock(Gfx &g, const Style &st, const float w[3], float time) {
    const char *title = "RTHIM";
    constexpr int kSize = 96;
    constexpr float kSpacing = 14.f;
    float tx = W * 0.5f;
    float ty = 110.f;

    if (w[0] > 0.01f) {
        float pulse = 0.85f + 0.15f * std::sin(time * 2.2f);
        for (int k = 0; k < 8; ++k) {
            float a = float(k) * kPi * 0.25f;
            g.text(title, kSize, fade(st.accent, 0.08f * w[0] * pulse),
                   tx + std::cos(a) * 3.f, ty + std::sin(a) * 3.f, Gfx::Center,
                   kSpacing, true);
        }
        g.text(title, kSize, fade(st.accent2, 0.22f * w[0]), tx + 3.f, ty,
               Gfx::Center, kSpacing, true);
    }

    if (w[1] > 0.01f) {
        g.glowCircle(tx, ty + 58.f, 240.f, st.accent, 0.13f * w[1]);
    }

    if (w[2] > 0.01f) {
        g.text(title, kSize, fade(hex(0x15130F), 0.9f * w[2]), tx + 9.f,
               ty + 9.f, Gfx::Center, kSpacing, true);
        g.fillRect({tx - 172.f, ty + 104.f, 344.f, 3.f},
                   fade(hex(0x15130F), 0.85f * w[2]));
    }

    g.text(title, kSize, st.text, tx, ty, Gfx::Center, kSpacing, true);

    g.text("RHYTHM  ·  TIMING  ·  STYLE", 11, fade(st.dim, 0.85f), tx, 236.f,
           Gfx::Center, 5.f, false);
}

void button(Gfx &g, const Style &st, Rect r, float hover, float press,
            const char *labelText, const char *num, float peak) {
    float h = r.h;
    float sw = 1.f + 0.022f * hover - 0.02f * press;
    Rect rr{r.x - (r.w * sw - r.w) * 0.5f, r.y - (h * sw - h) * 0.5f,
            r.w * sw, h * sw};
    float rad = st.radiusFactor * h * 0.5f;
    float cy = r.y + h * 0.5f;

    // мягкая тень (BLOOM)
    if (st.softShadow > 0.2f) {
        float k = st.softShadow / 9.f;
        for (int i = 3; i >= 1; --i) {
            float spread = float(i) * 3.f;
            g.fillRoundRect({rr.x - spread * 0.25f,
                             rr.y + st.softShadow * 0.55f,
                             rr.w + spread * 0.5f, rr.h},
                            rad + spread * 0.25f, st.shear,
                            fade(hex(0x7A3A52), 0.05f * k));
        }
    }

    // жёсткая тень (INK)
    if (st.hardShadow > 0.2f) {
        float k = std::min(1.f, st.hardShadow / 9.f);
        g.fillRoundRect({rr.x + st.hardShadow, rr.y + st.hardShadow, rr.w, rr.h},
                        rad, st.shear, fade(hex(0x000000), 0.9f * k));
    }

    // неоновое свечение (NEON)
    if (st.glow > 0.01f) {
        float gl = st.glow * (0.18f + 0.82f * hover);
        g.fillRoundRect(rr, rad, st.shear, fade(st.accent, 0.0f)); // выравнивание
        g.fillRoundRect(rr.inflated(3.f), rad + 3.f, st.shear,
                        fade(st.accent, 0.09f * gl));
        g.fillRoundRect(rr.inflated(8.f), rad + 8.f, st.shear,
                        fade(st.accent, 0.05f * gl));
    }

    Color fill = st.btnFill;
    Color border = st.btnBorder;
    if (peak > 0.01f) {
        fill = mix(fill, st.accent, 0.18f * peak);
    }
    if (hover > 0.f) {
        fill = mix(fill, st.accent, hover * 0.92f);
        border = mix(border, st.accent, hover);
    }

    if (st.borderWidth > 0.35f) {
        g.fillRoundRect(rr, rad, st.shear, border);
        float bw = st.borderWidth;
        g.fillRoundRect({rr.x + bw, rr.y + bw, rr.w - bw * 2.f, rr.h - bw * 2.f},
                        std::max(0.f, rad - bw), st.shear, fill);
    } else {
        g.fillRoundRect(rr, rad, st.shear, fill);
    }

    Color txt = mix(st.btnText, st.btnTextHover, hover);
    float lumAccent = luminance(st.accent);
    if (hover > 0.4f && std::fabs(lumAccent - luminance(txt)) < 0.25f) {
        txt = (lumAccent < 0.55f) ? hex(0xFFFFFF) : hex(0x0B0616);
    }
    float th = g.textHeight(19, true);
    g.text(labelText, 19, txt, rr.x + rr.w * 0.5f, cy - th * 0.5f - 1.f,
           Gfx::Center, 3.f, true);

    g.text(num, 10, fade(txt, 0.35f + 0.25f * hover), rr.x + 26.f,
           cy - 7.f, Gfx::Left, 2.f, false);

    // стрелка
    float ax = rr.x + rr.w - 30.f;
    Color arrow = fade(txt, 0.35f + 0.45f * hover);
    g.line(ax, cy - 5.f, ax + 6.f, cy, 1.6f, arrow);
    g.line(ax + 6.f, cy, ax, cy + 5.f, 1.6f, arrow);
}

void footer(Gfx &g, const Style &st, const State &s) {
    constexpr float trackW = 320.f;
    constexpr float trackX = (W - trackW) * 0.5f;
    constexpr float trackY = 660.f;

    g.fillRect({trackX, trackY - 1.f, trackW, 2.f}, fade(st.dim, 0.22f));

    int active = int(std::round(s.styleT));
    active = std::clamp(active, 0, 2);

    for (int k = 0; k < 3; ++k) {
        float x = trackX + trackW * (float(k) * 0.5f);
        bool on = (k == active);
        g.fillRect({x - 0.5f, trackY - 5.f, 1.f, 10.f},
                   fade(st.dim, on ? 0.55f : 0.22f));
        g.text(kNames[k], 10, on ? st.accent : fade(st.dim, 0.45f), x, 634.f,
               Gfx::Center, 3.f, on);
    }

    float kx = trackX + trackW * (s.styleT * 0.5f);
    g.glowCircle(kx, trackY, 20.f, st.accent, 0.30f);
    g.circle(kx, trackY, 4.5f, st.accent);

    g.text("RTHIM / 0.1 — PROTOTYPE", 10, fade(st.dim, 0.45f), 24.f, 678.f,
           Gfx::Left, 2.f, false);
    g.text("MOVE THE CURSOR — THE INTERFACE FOLLOWS", 10, fade(st.dim, 0.55f),
           W - 24.f, 678.f, Gfx::Right, 2.f, false);

    if (s.toast > 0.f) {
        float a = clamp01(s.toast / 0.35f);
        g.text(s.toastText, 11, fade(st.accent, 0.9f * a), W * 0.5f, 604.f,
               Gfx::Center, 3.f, true);
    }
}

void cursor(Gfx &g, const Style &st, const float w[3], float x, float y) {
    if (w[0] > 0.01f) {
        Color c = fade(st.accent, 0.85f * w[0]);
        g.line(x - 15.f, y, x + 15.f, y, 1.3f, c);
        g.line(x, y - 15.f, x, y + 15.f, 1.3f, c);
        g.ring(x, y, 9.f, 1.2f, fade(st.accent, 0.55f * w[0]));
    }
    if (w[1] > 0.01f) {
        g.glowCircle(x, y, 26.f, st.accent, 0.22f * w[1]);
        g.circle(x, y, 5.f, fade(st.accent, w[1]), 24);
        g.ring(x, y, 13.f, 1.2f, fade(st.accent, 0.35f * w[1]));
    }
    if (w[2] > 0.01f) {
        Color c = fade(st.text, w[2]);
        float d = 8.f;
        g.line(x - d, y - d, x + d, y - d, 2.f, c);
        g.line(x + d, y - d, x + d, y + d, 2.f, c);
        g.line(x + d, y + d, x - d, y + d, 2.f, c);
        g.line(x - d, y + d, x - d, y - d, 2.f, c);
    }
}

} // namespace

void update(State &s, float dt) {
    s.time += dt;
    s.styleT = styleCoordinate(clamp01(s.cursorX / W));

    int over = hitTest(s.cursorX, s.cursorY);
    if (over >= 0) {
        s.selected = over;
    }

    for (int i = 0; i < kCount; ++i) {
        float target = (i == s.selected) ? 1.f : 0.f;
        float k = 1.f - std::exp(-dt * 14.f);
        s.hover[i] += (target - s.hover[i]) * k;
        s.press[i] *= std::exp(-dt * 10.f);
    }
    if (s.toast > 0.f) {
        s.toast = std::max(0.f, s.toast - dt);
    }
}

void draw(Gfx &g, State &s) {
    Style st = blendStyle(s.styleT);
    float w[3];
    styleWeights(s.styleT, w);

    backdrop(g, st, w, s.time, s.cursorX, s.cursorY);
    titleBlock(g, st, w, s.time);

    float frac = std::fmod(s.styleT, 1.f);
    // peak = 1 ровно в середине перехода между мирами
    float peak = 4.f * frac * (1.f - frac);

    for (int i = 0; i < kCount; ++i) {
        button(g, st, rectFor(i), s.hover[i], s.press[i], kLabels[i],
               kNums[i], peak);
    }

    footer(g, st, s);
    cursor(g, st, w, s.cursorX, s.cursorY);
}

int hitTest(float x, float y) {
    for (int i = 0; i < kCount; ++i) {
        if (rectFor(i).contains(x, y)) {
            return i;
        }
    }
    return -1;
}

int buttonCount() { return kCount; }

const char *label(int index) {
    if (index < 0 || index >= kCount) {
        return "";
    }
    return kLabels[index];
}

} // namespace menu