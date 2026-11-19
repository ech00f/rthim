#include "menu.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace menu {
namespace {

constexpr float W = 1280.f;
constexpr float H = 720.f;
constexpr int kCount = 4;
constexpr float kPi = 3.14159265358979f;

// Раскладка интерактивных зон (совпадает с отрисовкой колонок).
constexpr float kEqX = 72.f;
constexpr float kEqBase = 530.f;
constexpr float kEqBarW = 20.f;
constexpr float kEqGap = 12.f;
constexpr float kEqMaxH = 200.f;
constexpr int kEqBars = 7;
constexpr float kEqW = float(kEqBars) * (kEqBarW + kEqGap) - kEqGap;
constexpr float kDotsX = kEqX + 96.f;
constexpr float kDotsY = kEqBase + 31.f;
constexpr float kTrackW = 300.f;
constexpr float kTrackX = (W - kTrackW) * 0.5f;
constexpr float kTrackY = 656.f;
constexpr float kHeroX = 1055.f;
constexpr float kHeroY = 408.f;
constexpr float kHeroR = 104.f;

const char *kLabels[kCount] = {"PLAY", "TRACKS", "SETTINGS", "QUIT"};
const char *kNames[3] = {"NEON", "BLOOM", "INK"};

enum IconKind { kPlay, kTracks, kSettings, kQuit };
const int kIcons[kCount] = {kPlay, kTracks, kSettings, kQuit};

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

float easeOutCubic(float x) {
    x = clamp01(x);
    float u = 1.f - x;
    return 1.f - u * u * u;
}

float easeOutQuint(float x) {
    x = clamp01(x);
    float u = 1.f - x;
    return 1.f - u * u * u * u * u;
}

// Детерминированный «шум» для декоративных частиц.
float hash01(int i) {
    float v = std::sin(float(i) * 12.9898f + 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

// Пружина: x стремится к target с жёсткостью k и демпфированием d.
// Даёт живое, чуть инерционное движение вместо линейного lerp.
void springTo(float &x, float &v, float target, float dt, float k, float d) {
    float a = (target - x) * k - v * d;
    v += a * dt;
    x += v * dt;
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

Rect rectFor(int i) {
    constexpr float bw = 460.f;
    constexpr float bh = 62.f;
    constexpr float x = (W - bw) * 0.5f;
    constexpr float y0 = 286.f;
    constexpr float gap = 14.f;
    return {x, y0 + float(i) * (bh + gap), bw, bh};
}

void icon(Gfx &g, int kind, float cx, float cy, float s, Color c) {
    switch (kind) {
    case kPlay: {
        std::vector<Pt> p{{cx - 4.f * s, cy - 6.5f * s},
                          {cx + 6.5f * s, cy},
                          {cx - 4.f * s, cy + 6.5f * s}};
        g.polygon(p, c);
        break;
    }
    case kTracks:
        g.fillRoundRect({cx - 8.f * s, cy - 7.f * s, 16.f * s, 1.8f * s},
                        0.9f * s, 0.f, c);
        g.fillRoundRect({cx - 8.f * s, cy - 1.4f * s, 10.5f * s, 1.8f * s},
                        0.9f * s, 0.f, c);
        g.fillRoundRect({cx - 8.f * s, cy + 4.2f * s, 14.f * s, 1.8f * s},
                        0.9f * s, 0.f, c);
        break;
    case kSettings:
        g.fillRoundRect({cx - 8.f * s, cy - 5.4f * s, 16.f * s, 1.6f * s},
                        0.8f * s, 0.f, c);
        g.fillRoundRect({cx - 8.f * s, cy + 3.6f * s, 16.f * s, 1.6f * s},
                        0.8f * s, 0.f, c);
        g.circle(cx + 3.6f * s, cy - 4.6f * s, 2.3f * s, c, 16);
        g.circle(cx - 3.2f * s, cy + 4.4f * s, 2.3f * s, c, 16);
        break;
    case kQuit:
        g.arc(cx, cy, 6.2f * s, 1.8f * s, -kPi * 0.34f, kPi * 1.34f, c, 26);
        g.fillRect({cx - 0.9f * s, cy - 8.6f * s, 1.8f * s, 5.6f * s}, c);
        break;
    default:
        break;
    }
}

int eqBarAt(float x) {
    float rel = x - kEqX;
    int i = int(std::floor((rel + kEqGap * 0.5f) / (kEqBarW + kEqGap)));
    return (i >= 0 && i < kEqBars) ? i : -1;
}

int bandOfBar(int i) { return (i < 3) ? 0 : ((i < 5) ? 1 : 2); }

int zoneAt(float x, float y) {
    if (x >= kDotsX - 16.f && x <= kDotsX + 4.f * 16.f + 12.f &&
        y >= kDotsY - 15.f && y <= kDotsY + 15.f) {
        return 4;
    }
    if (x >= kTrackX - 12.f && x <= kTrackX + kTrackW + 12.f &&
        y >= kTrackY - 18.f && y <= kTrackY + 18.f) {
        return 3;
    }
    if (x >= kEqX - 10.f && x <= kEqX + kEqW + 10.f &&
        y >= kEqBase - kEqMaxH - 24.f && y <= kEqBase + 10.f) {
        return 1;
    }
    float dx = x - kHeroX, dy = y - kHeroY;
    if (dx * dx + dy * dy <= 140.f * 140.f) {
        return 2;
    }
    return 0;
}

// Подсказка рядом с курсором: единственный способ рассказать,
// что декоративные на вид элементы тоже живые.
void tooltipDraw(Gfx &g, const Style &st, const State &s) {
    if (s.tipAlpha < 0.01f || s.tipText.empty()) {
        return;
    }
    float a = s.tipAlpha;
    float tw = g.textWidth(s.tipText, 10, 1.6f, true);
    float w = tw + 26.f;
    float h = 26.f;
    float x = s.cursorX + 20.f;
    float y = s.cursorY - 38.f;
    if (x + w > W - 14.f) {
        x = s.cursorX - 20.f - w;
    }
    x = std::max(14.f, x);
    if (y < 14.f) {
        y = s.cursorY + 28.f;
    }

    Color fill = st.text;
    Color ink = (luminance(fill) < 0.5f) ? hex(0xFFFFFF) : hex(0x141014);
    g.fillRoundRect({x, y, w, h}, 7.f, 0.f, fade(fill, 0.90f * a));
    g.fillRect({x + 12.f, y + 6.f, 1.f, h - 12.f}, fade(ink, 0.22f * a));
    g.text(s.tipText, 10, fade(ink, 0.95f * a), x + 18.f, y + 7.f,
           Gfx::Left, 1.6f, true);
}

void topBar(Gfx &g, const Style &st, const State &s) {
    float a = clamp01((s.intro - 0.10f) / 0.6f);
    if (a <= 0.002f) {
        return;
    }
    float pulse = 0.65f + 0.35f * std::sin(s.time * 2.2f);
    g.glowCircle(45.f, 40.f, 15.f, st.accent, 0.25f * a * pulse);
    g.circle(45.f, 40.f, 3.2f, fade(st.accent, a), 20);

    g.text("RTHIM", 12, fade(st.text, 0.92f * a), 58.f, 32.f, Gfx::Left, 3.f,
           true);
    float bw = g.textWidth("RTHIM", 12, 3.f, true);
    g.text("/ MAIN MENU", 10, fade(st.dim, 0.85f * a), 58.f + bw + 10.f,
           34.f, Gfx::Left, 2.f, false);
    g.text("MOVE THE CURSOR — THE WORLD FOLLOWS", 10,
           fade(st.dim, 0.8f * a), W - 44.f, 34.f, Gfx::Right, 2.5f, false);

    g.fillRect({44.f, 62.f, W - 88.f, 1.f}, fade(st.text, 0.10f * a));
}

void backdrop(Gfx &g, const Style &st, const float w[3], const State &s) {
    g.gradientRect({0.f, 0.f, W, H}, st.bgTop, st.bgBottom);

    float sc = clamp01(s.intro / 0.6f);
    float time = s.time;
    float px = clamp01(s.cursorX / W) - 0.5f;
    float py = clamp01(s.cursorY / H) - 0.5f;

    // ---- NEON: сетка, искры, скан, глитчи --------------------------------
    if (w[0] > 0.01f) {
        float a = w[0] * sc;
        Pt vp{640.f + px * 70.f, 420.f + py * 40.f};

        for (int i = -9; i <= 9; ++i) {
            float pulse = 0.5f + 0.5f * std::sin(time * 1.5f + i * 0.7f);
            g.line(vp.x, vp.y, 640.f + float(i) * 190.f, 800.f, 1.f,
                   fade(st.accent2,
                        (0.09f + 0.06f * pulse + 0.05f * s.beat) * a));
        }
        for (int k = 0; k < 12; ++k) {
            float f = std::fmod(time * 0.10f + float(k) / 12.f, 1.f);
            float y = vp.y + (H - vp.y + 80.f) * f * f;
            float half = 720.f * (0.3f + f);
            g.line(640.f - half, y, 640.f + half, y, 1.f,
                   fade(st.accent2, 0.13f * a * (1.f - f * 0.5f)));
        }

        float sy = std::fmod(time * 46.f, H + 260.f) - 130.f;
        g.gradientRect({0.f, sy - 80.f, W, 80.f}, withAlpha(st.accent2, 0.f),
                       fade(st.accent2, 0.045f * a));
        g.gradientRect({0.f, sy, W, 80.f}, fade(st.accent2, 0.045f * a),
                       withAlpha(st.accent2, 0.f));

        for (int i = 0; i < 34; ++i) {
            float sp = 26.f + hash01(i + 5) * 64.f;
            float y = H + 30.f -
                      std::fmod(hash01(i + 3) * (H + 220.f) + time * sp,
                                H + 220.f);
            float x = hash01(i + 9) * W + std::sin(time * 0.7f + i * 1.3f) * 16.f;
            float tw = 0.5f + 0.5f * std::sin(time * 3.2f + i * 1.7f);
            float r = 0.8f + hash01(i + 21) * 1.7f;
            Color c = (i % 3 == 0) ? st.accent2 : st.accent;
            g.circle(x, y, r, fade(c, 0.55f * a * tw), 8);
            if (i % 5 == 0) {
                g.glowCircle(x, y, r * 7.f, c, 0.10f * a * tw);
            }
        }

        int tick = int(time * 0.8f);
        if (hash01(tick * 13 + 5) > 0.9f) {
            float gy = hash01(tick * 7 + 1) * H;
            float gx = hash01(tick * 3 + 2) * (W - 140.f);
            float gw = 70.f + hash01(tick + 4) * 260.f;
            g.fillRect({gx, gy, gw, 2.f}, fade(st.accent2, 0.18f * a));
            g.fillRect({gx + 30.f, gy + 6.f, gw * 0.5f, 1.f},
                       fade(st.accent, 0.14f * a));
        }

        g.glowCircle(140.f + px * -90.f, 170.f + py * -60.f, 360.f, st.accent,
                     0.18f * a);
        g.glowCircle(1140.f + px * -90.f, 620.f + py * -50.f, 320.f,
                     st.accent2, 0.13f * a);

        Color bracket = fade(st.accent, 0.5f * a);
        auto corner = [&](float x, float y, float dx, float dy) {
            g.line(x, y, x + dx * 28.f, y, 2.f, bracket);
            g.line(x, y, x, y + dy * 28.f, 2.f, bracket);
        };
        corner(20.f, 20.f, 1.f, 1.f);
        corner(W - 20.f, 20.f, -1.f, 1.f);
        corner(20.f, H - 20.f, 1.f, -1.f);
        corner(W - 20.f, H - 20.f, -1.f, -1.f);
    }

    // ---- BLOOM: боке, лепестки, кольца ------------------------------------
    if (w[1] > 0.01f) {
        float a = w[1] * sc;

        for (int i = 0; i < 6; ++i) {
            float x = 120.f + hash01(i + 31) * 1040.f + px * -60.f;
            float y = 80.f + hash01(i + 41) * 560.f + py * -40.f;
            float r = 100.f + hash01(i + 51) * 170.f + s.beat * 12.f;
            float pulse = 0.5f + 0.5f * std::sin(time * 0.5f + float(i) * 1.7f);
            g.glowCircle(x, y, r, (i % 2) ? st.accent2 : st.accent,
                         (0.045f + 0.05f * pulse + 0.05f * s.beat) * a);
        }

        for (int i = 0; i < 20; ++i) {
            float speed = 10.f + hash01(i + 2) * 22.f;
            float y = H + 40.f -
                      std::fmod(hash01(i + 4) * (H + 80.f) + time * speed,
                                H + 120.f);
            float sway = std::sin(time * 0.9f + float(i) * 1.3f);
            float x = hash01(i + 11) * W + sway * 42.f;
            float rot = time * (0.4f + hash01(i + 8)) + float(i);
            float rx = 4.f + hash01(i + 6) * 5.f;
            Color c = (i % 2 == 0) ? st.accent : st.accent2;
            float al = (0.16f + 0.14f * hash01(i + 15)) * a;
            g.ellipse(x, y, rx, rx * 0.55f, rot, fade(c, al), 16);
        }

        g.ring(240.f + px * -30.f, 210.f + py * -20.f, 90.f, 1.2f,
               fade(st.accent2, 0.16f * a));
        g.ring(1180.f + px * -20.f, 130.f + py * -20.f, 70.f, 1.f,
               fade(st.accent2, 0.12f * a));
    }

    // ---- INK: полутон, пыль, красное кольцо, рамка ------------------------
    if (w[2] > 0.01f) {
        float a = w[2] * sc;

        for (float y = 16.f; y < H; y += 34.f) {
            for (float x = 16.f; x < W; x += 34.f) {
                float wave = 0.5f + 0.5f * std::sin(x * 0.012f + y * 0.02f + time * 1.3f);
                float r = 0.9f + 1.8f * (y / H) + 0.5f * wave +
                          0.35f * s.beat;
                g.circle(x + std::fmod(y, 34.f) * 0.5f, y, r,
                         fade(st.text, (0.05f + 0.04f * wave) * a), 6);
            }
        }

        for (int i = 0; i < 26; ++i) {
            float speed = 7.f + hash01(i + 6) * 16.f;
            float x = std::fmod(hash01(i + 2) * W + time * speed, W);
            float y = 40.f + hash01(i + 12) * (H - 100.f);
            float wlen = 4.f + hash01(i + 22) * 10.f;
            g.fillRect({x, y, wlen, 1.f}, fade(st.text, 0.10f * a));
        }

        for (int i = 0; i < 7; ++i) {
            float x = 860.f + float(i) * 46.f;
            g.line(x, -20.f, x - 190.f, 210.f, 22.f, fade(st.text, 0.04f * a));
        }

        Color frame = fade(st.text, 0.45f * a);
        g.line(16.f, 16.f, W - 16.f, 16.f, 1.2f, frame);
        g.line(16.f, H - 16.f, W - 16.f, H - 16.f, 1.2f, frame);
        g.line(16.f, 16.f, 16.f, H - 16.f, 1.2f, frame);
        g.line(W - 16.f, 16.f, W - 16.f, H - 16.f, 1.2f, frame);
    }

    // «Сумерки» на стыке миров + отклик на быстрый сдвиг стиля.
    float peak = 4.f * w[0] * w[1] + 4.f * w[1] * w[2];
    if (peak > 0.001f) {
        g.glowCircle(640.f, 330.f, 560.f, st.accent, 0.10f * peak * sc);
    }
    float shift = std::min(0.14f, std::fabs(s.styleV) * 0.02f);
    if (shift > 0.001f) {
        g.glowCircle(s.cursorX, s.cursorY, 260.f, st.accent, shift * sc);
    }
}

void titleBlock(Gfx &g, const Style &st, const float w[3], const State &s) {
    constexpr const char *kTitle = "RTHIM";
    constexpr int kSize = 96;
    float spacing = st.titleSpacing;

    float widths[5];
    float total = 0.f;
    for (int i = 0; i < 5; ++i) {
        widths[i] = g.textWidth(std::string(1, kTitle[i]), kSize, 0.f, true);
        total += widths[i];
    }
    total += spacing * 4.f;

    float x0 = W * 0.5f - total * 0.5f;
    float ty = 92.f;
    float tIn = clamp01((s.intro - 0.05f) / 0.7f);
    if (tIn <= 0.002f) {
        return;
    }

    if (w[0] > 0.01f) {
        float pulse = 0.75f + 0.45f * s.beat;
        for (int k = 0; k < 4; ++k) {
            float ang = float(k) * kPi * 0.5f;
            g.text(kTitle, kSize,
                   fade(st.accent, 0.09f * w[0] * pulse * tIn),
                   W * 0.5f + std::cos(ang) * 5.f,
                   ty + std::sin(ang) * 5.f, Gfx::Center, spacing, true);
        }
        g.text(kTitle, kSize, fade(st.accent2, 0.12f * w[0] * tIn),
               W * 0.5f + 2.f, ty, Gfx::Center, spacing, true);
    }
    if (w[1] > 0.01f) {
        g.glowCircle(W * 0.5f, ty + 52.f, 250.f, st.accent,
                     0.14f * w[1] * tIn);
    }
    if (w[2] > 0.01f) {
        float sh = 9.f + std::sin(s.time * 1.4f) * 1.5f;
        g.text(kTitle, kSize, fade(hex(0x15130F), 0.85f * w[2] * tIn),
               W * 0.5f + sh, ty + sh, Gfx::Center, spacing, true);
        g.fillRect({W * 0.5f - 172.f, ty + 110.f, 344.f * tIn, 3.f},
                   fade(hex(0x15130F), 0.8f * w[2] * tIn));
    }

    float px = x0;
    for (int i = 0; i < 5; ++i) {
        float d = clamp01((s.intro - 0.02f - float(i) * 0.045f) / 0.6f);
        float e = easeOutCubic(d);
        float yy = ty + (1.f - e) * 40.f +
                   std::sin(s.time * 1.7f + float(i) * 0.6f) * st.bob;
        g.text(std::string(1, kTitle[i]), kSize, fade(st.text, e), px, yy,
               Gfx::Left, 0.f, true);
        px += widths[i] + spacing;
    }

    float sub = clamp01((s.intro - 0.45f) / 0.6f);
    if (sub > 0.002f) {
        const char *line = "RHYTHM  ·  TIMING  ·  STYLE";
        float sw = g.textWidth(line, 11, 5.f, false);
        g.text(line, 11, fade(st.dim, 0.85f * sub), W * 0.5f, 214.f,
               Gfx::Center, 5.f, false);
        float grow = easeOutCubic(sub);
        g.fillRect({W * 0.5f - sw * 0.5f - 74.f, 222.f, 52.f * grow, 1.f},
                   fade(st.dim, 0.35f * sub));
        g.fillRect({W * 0.5f + sw * 0.5f + 22.f, 222.f, 52.f * grow, 1.f},
                   fade(st.dim, 0.35f * sub));
    }
}

void indicator(Gfx &g, const Style &st, const State &s) {
    float a = clamp01((s.intro - 0.55f) / 0.5f);
    if (a <= 0.002f) {
        return;
    }
    Rect r = rectFor(s.selected);
    float cy = s.indicatorY;
    float x = r.x - 22.f;
    float h = 20.f + 8.f * s.hover[s.selected];
    g.glowCircle(x + 1.5f, cy, 24.f, st.accent, 0.12f * a);
    g.fillRoundRect({x, cy - h * 0.5f, 3.5f, h}, 1.75f, 0.f,
                    fade(st.accent, 0.95f * a));
}

void button(Gfx &g, const Style &st, const State &s, int i, float peak) {
    Rect base = rectFor(i);
    float hover = s.hover[i];
    float press = s.press[i];

    float d = clamp01((s.intro - 0.22f - float(i) * 0.07f) / 0.6f);
    float e = easeOutCubic(d);
    if (e <= 0.002f) {
        return;
    }

    float yOff = (1.f - e) * 34.f;
    float scale = 1.f + 0.022f * hover - 0.022f * press;
    Rect r{base.x, base.y + yOff - 2.f * hover, base.w, base.h};
    Rect rr{r.x - (r.w * scale - r.w) * 0.5f,
            r.y - (r.h * scale - r.h) * 0.5f, r.w * scale, r.h * scale};
    float rad = st.radiusFactor * base.h * 0.5f;
    float cy = r.y + r.h * 0.5f;
    float a = e;

    if (st.softShadow > 0.2f) {
        float k = st.softShadow / 9.f;
        for (int j = 3; j >= 1; --j) {
            float spread = float(j) * 3.f;
            g.fillRoundRect({rr.x - spread * 0.25f,
                             rr.y + st.softShadow * 0.55f,
                             rr.w + spread * 0.5f, rr.h},
                            rad + spread * 0.25f, st.shear,
                            fade(hex(0x7A3A52), 0.05f * k * a));
        }
    }

    if (st.hardShadow > 0.2f) {
        float k = std::min(1.f, st.hardShadow / 9.f);
        g.fillRoundRect({rr.x + st.hardShadow, rr.y + st.hardShadow, rr.w, rr.h},
                        rad, st.shear, fade(hex(0x000000), 0.9f * k * a));
    }

    if (st.glow > 0.01f) {
        float gl = st.glow * (0.18f + 0.82f * hover + 0.30f * s.beat);
        g.fillRoundRect(rr.inflated(3.f), rad + 3.f, st.shear,
                        fade(st.accent, 0.09f * gl * a));
        g.fillRoundRect(rr.inflated(8.f), rad + 8.f, st.shear,
                        fade(st.accent, 0.05f * gl * a));
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
        g.fillRoundRect(rr, rad, st.shear, fade(border, a));
        float bw = st.borderWidth;
        g.fillRoundRect({rr.x + bw, rr.y + bw, rr.w - bw * 2.f, rr.h - bw * 2.f},
                        std::max(0.f, rad - bw), st.shear, fade(fill, a));
    } else {
        g.fillRoundRect(rr, rad, st.shear, fade(fill, a));
    }

    // блик сверху — тонкая деталь, которая сильно оживляет кнопку
    float gloss = std::max(st.glow, st.softShadow / 9.f);
    if (gloss > 0.05f && press < 0.5f) {
        g.fillRoundRect({rr.x + 14.f, rr.y + 7.f, rr.w - 28.f, 1.2f}, 0.6f,
                        0.f, fade(hex(0xFFFFFF), 0.10f * gloss * a));
    }

    if (press > 0.01f) {
        g.fillRoundRect(rr, rad, st.shear, fade(st.accent, 0.12f * press * a));
    }

    Color txt = mix(st.btnText, st.btnTextHover, hover);
    float lumAccent = luminance(st.accent);
    if (hover > 0.4f && std::fabs(lumAccent - luminance(txt)) < 0.25f) {
        txt = (lumAccent < 0.55f) ? hex(0xFFFFFF) : hex(0x0B0616);
    }

    Color iconCol = fade(mix(txt, st.accent, 0.15f * (1.f - hover)), 0.9f * a);
    icon(g, kIcons[i], rr.x + 36.f, cy, 1.f + 0.08f * hover, iconCol);

    float th = g.textHeight(19, true);
    g.text(kLabels[i], 19, fade(txt, a), rr.x + rr.w * 0.5f + 6.f,
           cy - th * 0.5f - 1.f, Gfx::Center, st.labelSpacing + 1.6f * hover,
           true);

    float ax = rr.x + rr.w - 32.f + 3.f * hover +
               std::sin(s.time * 3.f) * 1.2f * hover;
    Color arrow = fade(txt, (0.3f + 0.5f * hover) * a);
    g.line(ax, cy - 5.f, ax + 6.f, cy, 1.6f, arrow);
    g.line(ax + 6.f, cy, ax, cy + 5.f, 1.6f, arrow);
}

// Левая колонка: «сигнал» — псевдо-эквалайзер, который дышит в такт.
void equalizer(Gfx &g, const Style &st, const State &s) {
    float a = clamp01((s.intro - 0.30f) / 0.6f);
    if (a <= 0.002f) {
        return;
    }
    constexpr float x0 = kEqX;
    constexpr float base = kEqBase;
    constexpr float bw = kEqBarW;
    constexpr float gap = kEqGap;
    constexpr float maxH = kEqMaxH;
    constexpr int n = kEqBars;

    g.text("SIGNAL", 10, fade(st.dim, 0.7f * a), x0, 226.f, Gfx::Left, 3.f,
           false);
    g.text("STEREO", 9, fade(st.dim, 0.35f * a), x0, 242.f, Gfx::Left, 3.f,
           false);

    float trackW = kEqW;

    g.fillRect({x0, base - maxH, trackW, 1.f}, fade(st.text, 0.06f * a));
    g.fillRect({x0, base + 1.f, trackW, 1.f}, fade(st.text, 0.14f * a));

    for (int i = 0; i < n; ++i) {
        // Семь столбиков — это три реальные полосы, растянутые по ширине.
        float xf = float(i) / float(n - 1);
        float band = (xf < 0.5f) ? std::lerp(s.bandLow, s.bandMid, xf * 2.f)
                                 : std::lerp(s.bandMid, s.bandHigh,
                                             (xf - 0.5f) * 2.f);
        int bandIdx = bandOfBar(i);
        bool muted = s.mute[bandIdx];
        float hb = (s.hoverZone == 1 && bandIdx == s.hoverBand)
                       ? s.tipAlpha
                       : 0.f;
        float wobble = 0.94f + 0.06f * std::sin(s.time * 3.1f + float(i) * 1.3f);
        float shape = 0.10f + 0.90f * band * wobble;
        float h = muted ? 10.f : std::min(maxH, 18.f + maxH * shape);
        float x = x0 + float(i) * (bw + gap);
        float r = st.radiusFactor * bw * 0.5f;
        Color c = (i % 2 == 0) ? st.accent : st.accent2;
        if (muted) {
            c = st.dim;
        }

        g.fillRoundRect({x, base - maxH, bw, maxH}, r, 0.f,
                        fade(st.text, (0.05f + 0.05f * hb) * a));
        if (!muted && st.hardShadow > 0.2f) {
            float sh = st.hardShadow * 0.5f;
            g.fillRoundRect({x + sh, base - h + sh, bw, h}, r, 0.f,
                            fade(hex(0x000000), 0.85f * a));
        }
        float fillA = muted ? 0.16f : (0.40f + 0.55f * band + 0.20f * hb);
        g.fillRoundRect({x, base - h, bw, h}, r, 0.f, fade(c, fillA * a));
        if (!muted && st.glow > 0.05f) {
            g.glowCircle(x + bw * 0.5f, base - h * 0.5f, 20.f, c,
                         (0.09f + 0.06f * hb) * st.glow * a);
        }
        if (hb > 0.01f) {
            g.fillRect({x, base - h - 3.f, bw, 2.f},
                       fade(st.text, 0.55f * hb * a));
        }
    }

    float by = base + 24.f;
    float pulse = 0.6f + 0.4f * std::sin(s.time * 6.28318f * 2.f);
    g.glowCircle(x0 + 3.f, by + 7.f, 11.f, st.accent, 0.30f * a * pulse);
    g.circle(x0 + 3.f, by + 7.f, 3.f, fade(st.accent, a * pulse), 16);
    char bpmText[32];
    std::snprintf(bpmText, sizeof bpmText, "%d BPM", s.bpm);
    g.text(bpmText, 11, fade(st.dim, 0.8f * a), x0 + 15.f, by,
           Gfx::Left, 2.f, true);

    // Четыре доли такта: текущая вспыхивает на ударе и гаснет.
    float dotsX = x0 + 96.f;
    for (int i = 0; i < 4; ++i) {
        float lit = (i == s.beatIndex) ? (1.f - s.beatPhase) : 0.f;
        g.circle(dotsX + float(i) * 16.f, by + 7.f, 3.f,
                 fade(st.accent, (0.22f + 0.78f * lit) * a), 12);
    }
    if (s.hoverZone == 4) {
        g.ring(dotsX + 24.f, by + 7.f, 34.f, 1.f,
               fade(st.accent, 0.35f * s.tipAlpha * a));
    }
    g.text("4 / 4", 10, fade(st.dim, 0.4f * a), x0 + trackW, by + 1.f,
           Gfx::Right, 2.f, false);
}

// Правая колонка: медальон текущего мира и выбранного действия.
void hero(Gfx &g, const Style &st, const float w[3], const State &s) {
    float a = clamp01((s.intro - 0.35f) / 0.6f);
    if (a <= 0.002f) {
        return;
    }
    constexpr float cx = kHeroX;
    constexpr float cy = kHeroY;
    constexpr float R = kHeroR;
    float beat = s.beat;

    g.text("CURRENT WORLD", 10, fade(st.dim, 0.7f * a), cx, 226.f,
           Gfx::Center, 3.f, false);
    int active = std::clamp(int(std::round(s.styleT)), 0, 2);
    g.text(kNames[active], 22, fade(st.accent, a), cx, 242.f, Gfx::Center,
           2.f, true);

    // Орбита с делениями и вращающимися дугами.
    g.ring(cx, cy, R + 24.f, 1.f, fade(st.dim, 0.16f * a));
    for (int i = 0; i < 48; ++i) {
        float ang = 6.28318f * float(i) / 48.f + s.time * 0.06f;
        float r0 = R + 16.f;
        float r1 = r0 + ((i % 4 == 0) ? 7.f : 3.f);
        g.line(cx + std::cos(ang) * r0, cy + std::sin(ang) * r0,
               cx + std::cos(ang) * r1, cy + std::sin(ang) * r1, 1.f,
               fade(st.dim, ((i % 4 == 0) ? 0.35f : 0.14f) * a));
    }
    for (int k = 0; k < 3; ++k) {
        float base = s.time * (0.32f - 0.07f * float(k)) + float(k) * 2.4f;
        float span = 0.85f - 0.16f * float(k);
        g.arc(cx, cy, R + 6.f - float(k) * 9.f, 2.f, base, base + span,
              fade(st.accent, (0.50f - 0.12f * float(k)) * a), 40);
    }

    float discR = R - 10.f + beat * 2.f;

    // Бочка: от диска расходится короткое кольцо.
    if (beat > 0.02f) {
        float rr = discR + 6.f + 22.f * (1.f - beat);
        g.ring(cx, cy, rr, 1.2f + 2.6f * beat,
               fade(st.accent, 0.34f * beat * a));
    }

    // Свечение/тень под диском.
    if (w[0] > 0.01f) {
        g.glowCircle(cx, cy, R * 1.3f, st.accent,
                     (0.14f + 0.10f * beat) * w[0] * a);
    }
    if (w[1] > 0.01f) {
        g.glowCircle(cx, cy, R * 1.15f, st.accent,
                     (0.09f + 0.06f * beat) * w[1] * a);
    }
    if (w[2] > 0.01f) {
        g.circle(cx + 9.f, cy + 9.f, discR,
                 fade(hex(0x000000), 0.85f * w[2] * a), 64);
    }

    Color disc = mix(st.btnFill, st.bgBottom, 0.15f);
    if (w[2] > 0.01f) {
        disc = mix(disc, hex(0xF6F3EB), 0.85f * w[2]);
    }
    g.circle(cx, cy, discR, fade(disc, a), 64);
    if (st.borderWidth > 0.35f) {
        g.ring(cx, cy, discR, st.borderWidth, fade(st.btnBorder, a));
    }

    // Внутренности диска — свои у каждого мира.
    if (w[0] > 0.01f) {
        for (int i = 0; i < 32; ++i) {
            float ang = 6.28318f * float(i) / 32.f - s.time * 0.35f;
            float r0 = discR * 0.40f;
            float amp = 0.35f + 0.65f * std::fabs(
                                         std::sin(float(i) * 2.1f + s.time * 2.6f));
            float r1 = r0 + 10.f + discR * 0.34f * amp + beat * 14.f;
            g.line(cx + std::cos(ang) * r0, cy + std::sin(ang) * r0,
                   cx + std::cos(ang) * r1, cy + std::sin(ang) * r1, 2.f,
                   fade((i % 2) ? st.accent2 : st.accent, 0.45f * w[0] * a));
        }
        g.ring(cx, cy, discR * 0.36f, 1.2f, fade(st.accent, 0.40f * w[0] * a));
    }
    if (w[1] > 0.01f) {
        for (int i = 0; i < 3; ++i) {
            g.ring(cx, cy, discR * (0.45f + 0.20f * float(i)), 1.6f,
                   fade(st.accent2, 0.34f * w[1] * a));
        }
        for (int i = 0; i < 8; ++i) {
            float ang = s.time * 0.25f + 6.28318f * float(i) / 8.f;
            float rr = discR + 18.f + std::sin(s.time * 1.2f + float(i)) * 4.f +
                       beat * 8.f;
            g.ellipse(cx + std::cos(ang) * rr, cy + std::sin(ang) * rr, 9.f,
                      4.4f, ang, fade(st.accent, 0.45f * w[1] * a), 18);
        }
    }
    if (w[2] > 0.01f) {
        std::vector<Pt> wedge;
        wedge.push_back({cx, cy});
        float baseAng = s.time * 0.18f + s.beat * 0.10f;
        for (int i = 0; i <= 24; ++i) {
            float ang = baseAng + 1.10f * float(i) / 24.f;
            wedge.push_back({cx + std::cos(ang) * discR,
                             cy + std::sin(ang) * discR});
        }
        g.polygon(wedge, fade(st.accent2, 0.85f * w[2] * a));

    }

    if (w[2] > 0.01f) {
        g.circle(cx, cy, 42.f, fade(hex(0xF6F3EB), 0.96f * w[2] * a), 40);
        g.ring(cx, cy, 42.f, 2.f, fade(hex(0x15130F), 0.9f * w[2] * a));
    }

    // Большая иконка выбранного пункта.
    float pop = 1.f + 0.14f * std::exp(-s.iconT * 9.f);
    Color ic = st.accent;
    if (std::fabs(luminance(ic) - luminance(disc)) < 0.22f) {
        ic = st.text;
    }
    icon(g, kIcons[s.selected], cx, cy - 6.f, 2.6f * pop,
         fade(ic, 0.95f * a));

    // Три мира на орбите: активный крупнее — по ним можно кликать/тянуть.
    for (int k = 0; k < 3; ++k) {
        float ang = -kPi * 0.5f + float(k) * (kPi * 2.f / 3.f);
        float ox = cx + std::cos(ang) * (R + 24.f);
        float oy = cy + std::sin(ang) * (R + 24.f);
        float on = 1.f - std::min(1.f, std::fabs(s.styleT - float(k)));
        g.glowCircle(ox, oy, 15.f, st.accent, (0.05f + 0.22f * on) * a);
        g.circle(ox, oy, 3.f + 2.2f * on,
                 fade(st.accent, (0.35f + 0.65f * on) * a), 18);
    }

    if (s.hoverZone == 2 || (s.dragging && s.dragKind == 2)) {
        float ha = (s.dragging && s.dragKind == 2) ? 1.f : s.tipAlpha;
        g.ring(cx, cy, R + 33.f + 2.f * std::sin(s.time * 4.f), 1.2f,
               fade(st.accent, 0.35f * ha * a));
    }

    // Показания под медальоном.
    const char *statLabels[3] = {"TEMPO", "OFFSET", "SYNC"};
    char tempoText[8];
    std::snprintf(tempoText, sizeof tempoText, "%d", s.bpm);
    const char *statVals[3] = {tempoText, "+12 MS", "100%"};
    for (int i = 0; i < 3; ++i) {
        float x = cx + float(i - 1) * 92.f;
        g.text(statLabels[i], 9, fade(st.dim, 0.6f * a), x, 566.f,
               Gfx::Center, 2.f, false);
        g.text(statVals[i], 16, fade(st.text, 0.9f * a), x, 580.f,
               Gfx::Center, 1.f, true);
        if (i < 2) {
            g.fillRect({x + 54.f, 568.f, 1.f, 34.f}, fade(st.text, 0.08f * a));
        }
    }
}

void footer(Gfx &g, const Style &st, const State &s) {
    float a = clamp01((s.intro - 0.5f) / 0.6f);
    if (a <= 0.002f) {
        return;
    }
    constexpr float trackW = kTrackW;
    constexpr float trackX = kTrackX;
    constexpr float trackY = kTrackY;

    float hb = (s.hoverZone == 3 || (s.dragging && s.dragKind == 1))
                   ? std::max(s.tipAlpha, s.dragging ? 1.f : 0.f)
                   : 0.f;
    g.fillRect({trackX, trackY - 1.f, trackW, 2.f},
               fade(mix(st.dim, st.accent, hb), (0.20f + 0.35f * hb) * a));

    int active = std::clamp(int(std::round(s.styleT)), 0, 2);
    for (int k = 0; k < 3; ++k) {
        float x = trackX + trackW * 0.5f * float(k);
        bool on = (k == active);
        g.fillRect({x - 0.5f, trackY - 5.f, 1.f, 10.f},
                   fade(st.dim, (on ? 0.55f : 0.20f) * a));
        g.text(kNames[k], on ? 11 : 10,
               on ? fade(st.accent, a) : fade(st.dim, 0.45f * a), x,
               on ? 626.f : 628.f, Gfx::Center, 3.f, on);
    }

    float kx = trackX + trackW * (s.styleT * 0.5f);
    float pulse = 0.65f + 0.5f * s.beat;
    float kr = 4.5f + 2.5f * hb;
    g.glowCircle(kx, trackY, 18.f + 6.f * hb, st.accent,
                 (0.35f + 0.25f * hb) * a * pulse);
    g.circle(kx, trackY, kr, fade(st.accent, a), 24);
    if (hb > 0.01f) {
        g.ring(kx, trackY, kr + 5.f, 1.f, fade(st.text, 0.4f * hb * a));
    }

    g.text("RTHIM / 0.1 — PROTOTYPE", 10, fade(st.dim, 0.55f * a), 44.f,
           684.f, Gfx::Left, 2.f, false);
    g.text("UP/DOWN ENTER · ESC QUIT", 10, fade(st.dim, 0.55f * a),
           W - 44.f, 684.f, Gfx::Right, 2.f, false);
}

void toastDraw(Gfx &g, const Style &st, const State &s) {
    if (s.toast <= 0.f) {
        return;
    }
    float a = std::min(clamp01((1.8f - s.toast) / 0.18f),
                       clamp01(s.toast / 0.45f));
    float y = 602.f + (1.f - a) * 8.f;
    g.text(s.toastText, 11, fade(st.accent, 0.9f * a), W * 0.5f, y,
           Gfx::Center, 3.f, true);
}

void ripple(Gfx &g, const Style &st, const State &s) {
    if (s.rippleT > 0.9f) {
        return;
    }
    float t = clamp01(s.rippleT / 0.9f);
    float r = 12.f + easeOutQuint(t) * 120.f;
    float a = 1.f - t;
    g.glowCircle(s.rippleX, s.rippleY, r, st.accent, 0.10f * a);
    g.ring(s.rippleX, s.rippleY, r, 2.f, fade(st.accent, 0.45f * a));
    g.ring(s.rippleX, s.rippleY, r * 0.62f, 1.f, fade(st.accent, 0.25f * a));
}

void cursorDraw(Gfx &g, const Style &st, const float w[3], const State &s) {
    float x = s.cursorX, y = s.cursorY;
    float hov = 0.f;
    for (int i = 0; i < kCount; ++i) {
        hov = std::max(hov, s.hover[i]);
    }
    float k = 1.f + 0.35f * hov;
    // На залитой акцентом кнопке курсор того же цвета исчезал бы —
    // подмешиваем цвет текста, он всегда контрастен плашке.
    Color cc = mix(st.accent, st.text, hov * 0.85f);

    g.glowCircle(s.trailX, s.trailY, 26.f * k, st.accent,
                 0.10f + 0.10f * hov + 0.08f * s.beat);

    if (w[0] > 0.01f) {
        Color c = fade(cc, 0.85f * w[0]);
        g.line(x - 15.f * k, y, x + 15.f * k, y, 1.3f, c);
        g.line(x, y - 15.f * k, x, y + 15.f * k, 1.3f, c);
        g.ring(x, y, 9.f * k, 1.2f, fade(cc, 0.55f * w[0]));
    }
    if (w[1] > 0.01f) {
        g.circle(x, y, 4.6f * k, fade(cc, w[1]), 24);
        g.ring(x, y, 13.f * k, 1.2f, fade(cc, 0.35f * w[1]));
    }
    if (w[2] > 0.01f) {
        Color c = fade(cc, w[2]);
        float d = 8.f * k;
        g.line(x - d, y - d, x + d, y - d, 2.f, c);
        g.line(x + d, y - d, x + d, y + d, 2.f, c);
        g.line(x + d, y + d, x - d, y + d, 2.f, c);
        g.line(x - d, y + d, x - d, y - d, 2.f, c);
    }
}

} // namespace

void updateCore(State &s, float dt) {
    dt = std::min(dt, 1.f / 30.f);
    s.time += dt;
    s.intro += dt;

    // Ручное управление миром: тянем шкалу или медальон. Держится,
    // пока курсор не уедет далеко от точки отпускания.
    if (s.dragging && s.pointerDown) {
        if (s.dragKind == 1) {
            s.override = true;
            s.overrideT = clamp01((s.rawX - kTrackX) / kTrackW) * 2.f;
            s.overrideAnchorX = s.rawX;
        } else if (s.dragKind == 2) {
            s.override = true;
            s.overrideT = std::clamp(
                s.dragStartT + (s.rawX - s.dragStartX) / 260.f, 0.f, 2.f);
        }
    }
    if (s.override && !s.dragging && !s.pointerDown &&
        std::fabs(s.rawX - s.overrideAnchorX) > 70.f) {
        s.override = false;
    }

    if (s.override) {
        s.styleTarget = s.overrideT;
    } else if (s.worldMode == 1) {
        s.autoT += dt * 0.055f * std::max(0.2f, s.autoSpeed);
        s.styleTarget = 1.f - std::cos(s.autoT * 6.28318f);
    } else if (s.worldMode == 2) {
        s.styleTarget = float(std::clamp(s.staticWorld, 0, 2));
    } else {
        s.styleTarget = styleCoordinate(clamp01(s.rawX / W));
    }

    springTo(s.styleT, s.styleV, s.styleTarget, dt, 64.f, 15.5f);
    springTo(s.cursorX, s.cursorVX, s.rawX, dt, 900.f, 60.f);
    springTo(s.cursorY, s.cursorVY, s.rawY, dt, 900.f, 60.f);
    springTo(s.trailX, s.trailVX, s.rawX, dt, 110.f, 21.f);
    springTo(s.trailY, s.trailVY, s.rawY, dt, 110.f, 21.f);

    if (s.rippleT < 2.f) {
        s.rippleT += dt;
    }
    if (s.toast > 0.f) {
        s.toast = std::max(0.f, s.toast - dt);
    }
}

void update(State &s, float dt) {
    dt = std::min(dt, 1.f / 30.f);
    updateCore(s, dt);

    int over = hitTest(s.rawX, s.rawY);
    if (over >= 0) {
        s.selected = over;
    }
    if (s.selected != s.prevSelected) {
        s.prevSelected = s.selected;
        s.iconT = 0.f;
    }
    s.iconT += dt;

    for (int i = 0; i < kCount; ++i) {
        float target = (i == s.selected) ? 1.f : 0.f;
        float k = 1.f - std::exp(-dt * 16.f);
        s.hover[i] += (target - s.hover[i]) * k;
        s.press[i] *= std::exp(-dt * 9.f);
    }

    float targetY = rectFor(s.selected).y + rectFor(s.selected).h * 0.5f;
    if (s.indicatorY < 1.f) {
        s.indicatorY = targetY;
    }
    springTo(s.indicatorY, s.indicatorV, targetY, dt, 140.f, 18.f);

    // Подсказки к не-кнопочным зонам.
    bool overButton = hitTest(s.rawX, s.rawY) >= 0;
    int zone = overButton ? 0 : zoneAt(s.rawX, s.rawY);
    s.hoverZone = zone;
    if (zone == 1) {
        int i = eqBarAt(s.rawX);
        if (i >= 0) {
            s.hoverBand = bandOfBar(i);
        }
    }
    float tipTarget = (zone != 0) ? 1.f : 0.f;
    s.tipAlpha += (tipTarget - s.tipAlpha) * (1.f - std::exp(-dt * 22.f));

    static const char *kBandNames[3] = {"LOW", "MID", "HIGH"};
    if (zone == 1) {
        s.tipText = std::string(kBandNames[s.hoverBand]) +
                    (s.mute[s.hoverBand] ? "  ·  CLICK TO UNMUTE"
                                         : "  ·  CLICK TO MUTE");
    } else if (zone == 2) {
        s.tipText = "DRAG / CLICK  ·  WORLD";
    } else if (zone == 3) {
        s.tipText = "DRAG  ·  WORLD";
    } else if (zone == 4) {
        s.tipText = "CLICK  ·  BAR RESTART";
    } else {
        s.tipText.clear();
    }
}

void draw(Gfx &g, State &s) {
    Style st = blendStyle(s.styleT);
    float w[3];
    styleWeights(s.styleT, w);

    backdrop(g, st, w, s);
    equalizer(g, st, s);
    hero(g, st, w, s);
    topBar(g, st, s);
    titleBlock(g, st, w, s);
    indicator(g, st, s);

    float frac = std::fmod(s.styleT, 1.f);
    float peak = 4.f * frac * (1.f - frac);
    for (int i = 0; i < kCount; ++i) {
        button(g, st, s, i, peak);
    }

    footer(g, st, s);
    toastDraw(g, st, s);
    ripple(g, st, s);
    tooltipDraw(g, st, s);
    cursorDraw(g, st, w, s);
}

void drawCursor(Gfx &g, State &s) {
    Style st = blendStyle(s.styleT);
    float w[3];
    styleWeights(s.styleT, w);
    cursorDraw(g, st, w, s);
}

void activate(State &s, int index) {
    if (index < 0 || index >= kCount) {
        return;
    }
    s.press[index] = 1.f;
    Rect r = rectFor(index);
    s.rippleX = r.x + r.w * 0.5f;
    s.rippleY = r.y + r.h * 0.5f;
    s.rippleT = 0.f;
    // Экраны теперь настоящие: тост «not wired» больше не нужен.
    s.toast = 0.f;
}

int handleClick(State &s, float x, float y) {
    s.rawX = x;
    s.rawY = y;

    int b = hitTest(x, y);
    if (b >= 0) {
        s.selected = b;
        return b;
    }

    int zone = zoneAt(x, y);
    auto pulse = [&](float px, float py) {
        s.rippleX = px;
        s.rippleY = py;
        s.rippleT = 0.f;
    };

    if (zone == 1) {
        int i = eqBarAt(x);
        if (i >= 0) {
            int band = bandOfBar(i);
            s.mute[band] = !s.mute[band];
            static const char *names[3] = {"LOW", "MID", "HIGH"};
            toast(s, std::string(names[band]) +
                         (s.mute[band] ? " MUTED" : " ON"));
            pulse(x, y);
        }
    } else if (zone == 4) {
        s.requestBarRestart = true;
        toast(s, "BAR RESTART");
        pulse(x, y);
    } else if (zone == 3) {
        s.dragging = true;
        s.dragKind = 1;
        s.override = true;
        s.overrideT = clamp01((x - kTrackX) / kTrackW) * 2.f;
        s.overrideAnchorX = x;
        pulse(x, y);
    } else if (zone == 2) {
        s.dragging = true;
        s.dragKind = 2;
        s.dragStartX = x;
        s.dragStartY = y;
        s.dragStartT = s.override ? s.overrideT : s.styleT;
    }
    return -1;
}

void handleRelease(State &s) {
    float moved = std::hypot(s.rawX - s.dragStartX, s.rawY - s.dragStartY);
    if (s.dragging && s.dragKind == 2 && moved < 7.f) {
        // Короткий клик по медальону — следующий мир.
        float cur = s.override ? s.overrideT : s.styleT;
        int next = (int(std::lround(cur)) + 1) % 3;
        s.override = true;
        s.overrideT = float(next);
        s.rippleX = kHeroX;
        s.rippleY = kHeroY;
        s.rippleT = 0.f;
        static const char *worlds[3] = {"NEON", "BLOOM", "INK"};
        toast(s, std::string(worlds[next]) + " WORLD");
    }
    s.dragging = false;
    s.dragKind = 0;
    s.overrideAnchorX = s.rawX;
}

void toast(State &s, const std::string &text) {
    s.toastText = text;
    s.toast = 1.6f;
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

Pt buttonCenter(int index) {
    Rect r = rectFor(index);
    return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
}

} // namespace menu