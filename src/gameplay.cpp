#include "gameplay.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game {
namespace {

constexpr float W = 1280.f;
constexpr float H = 720.f;
constexpr float CX = 640.f;
constexpr float CY = 356.f;
constexpr float RING = 196.f;
constexpr float SPAWN_R = 700.f;
constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530718f;

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

float easeOutCubic(float x) {
    x = clamp01(x);
    float u = 1.f - x;
    return 1.f - u * u * u;
}

float hash01(int i) {
    float v = std::sin(float(i) * 12.9898f + 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

float angDiff(float a, float b) {
    return std::atan2(std::sin(a - b), std::cos(a - b));
}

float wrapAngle(float a) {
    while (a > kPi) {
        a -= kTau;
    }
    while (a < -kPi) {
        a += kTau;
    }
    return a;
}

Color contrastOn(const Color &bg, const Color &text) {
    if (std::fabs(luminance(bg) - luminance(text)) < 0.28f) {
        return (luminance(bg) < 0.5f) ? hex(0xFFFFFF) : hex(0x141014);
    }
    return text;
}

const char *judgeName(int j) {
    switch (j) {
    case kPerfect:
        return "PERFECT";
    case kGood:
        return "GOOD";
    case kOk:
        return "OK";
    default:
        return "MISS";
    }
}

Color judgeColor(const Style &st, int j) {
    switch (j) {
    case kPerfect:
        return st.accent;
    case kGood:
        return mix(st.accent, st.text, 0.35f);
    case kOk:
        return st.dim;
    default:
        return (st.name == std::string("INK")) ? hex(0xE2452B) : hex(0xFF5577);
    }
}

void stage(Gfx &g, const Style &st, const State &s, float beatPulse) {
    g.gradientRect({0, 0, W, H}, st.bgTop, st.bgBottom);

    // Мягкое пятно в центре, дышащее в такт.
    g.glowCircle(CX, CY, 430.f, st.accent,
                 0.05f + 0.07f * beatPulse + 0.04f * s.flash);
    g.glowCircle(CX, CY, 240.f, st.accent2, 0.04f + 0.05f * beatPulse);

    // Дальние кольца сцены.
    for (int i = 1; i <= 3; ++i) {
        float r = RING * (1.25f + 0.32f * float(i));
        g.ring(CX, CY, r, 1.f,
               fade(st.text, (0.05f - 0.012f * float(i))));
    }

    // Пыль, медленно плывущая по кругу.
    for (int i = 0; i < 26; ++i) {
        float a = hash01(i * 3 + 1) * kTau + s.songMs * 0.000012f *
                                                  (1.f + hash01(i + 7));
        float r = 230.f + hash01(i * 5 + 2) * 340.f;
        float tw = 0.4f + 0.6f * std::sin(s.songMs * 0.0015f + float(i));
        g.circle(CX + std::cos(a) * r, CY + std::sin(a) * r,
                 0.8f + 1.4f * hash01(i + 11),
                 fade(st.text, 0.05f + 0.06f * tw));
    }
}

void ringAndArc(Gfx &g, const Style &st, const State &s, float ox, float oy) {
    // Внутренний диск.
    g.circle(CX + ox, CY + oy, RING - 10.f,
             fade(mix(st.bgBottom, st.btnFill, 0.25f), 0.55f), 72);
    g.ring(CX + ox, CY + oy, RING - 10.f, 1.f, fade(st.text, 0.10f));

    // Деления по всему кольцу.
    for (int i = 0; i < 48; ++i) {
        float a = kTau * float(i) / 48.f;
        bool big = (i % 4 == 0);
        float r0 = RING + 5.f;
        float r1 = r0 + (big ? 10.f : 5.f);
        g.line(CX + ox + std::cos(a) * r0, CY + oy + std::sin(a) * r0,
               CX + ox + std::cos(a) * r1, CY + oy + std::sin(a) * r1,
               big ? 1.6f : 1.f, fade(st.text, big ? 0.26f : 0.12f));
    }

    g.ring(CX + ox, CY + oy, RING, 2.f, fade(st.text, 0.35f));

    float a0 = s.arcAngle - s.arcHalf;
    float a1 = s.arcAngle + s.arcHalf;
    g.glowCircle(CX + ox + std::cos(s.arcAngle) * RING,
                 CY + oy + std::sin(s.arcAngle) * RING, 120.f, st.accent,
                 0.16f);
    g.arc(CX + ox, CY + oy, RING + 1.f, 22.f, a0, a1,
          fade(st.accent, 0.22f), 48);
    g.arc(CX + ox, CY + oy, RING + 1.f, 3.f, a0, a1, fade(st.accent, 0.95f),
          48);
    for (int e = 0; e < 2; ++e) {
        float a = (e == 0) ? a0 : a1;
        g.line(CX + ox + std::cos(a) * (RING - 16.f),
               CY + oy + std::sin(a) * (RING - 16.f),
               CX + ox + std::cos(a) * (RING + 30.f),
               CY + oy + std::sin(a) * (RING + 30.f), 2.f,
               fade(st.accent, 0.85f));
    }

    // Указатель в середине дуги — видно, куда целишься.
    float ax = CX + ox + std::cos(s.arcAngle) * (RING - 26.f);
    float ay = CY + oy + std::sin(s.arcAngle) * (RING - 26.f);
    g.circle(ax, ay, 3.4f, fade(st.text, 0.85f), 20);
    g.glowCircle(ax, ay, 16.f, st.accent, 0.35f);
}

void meteor(Gfx &g, const Style &st, const State &s, const Spawn &n,
            size_t idx, float ox, float oy, double now, double okWindow) {
    double tNote = n.timeMs;
    double total = std::max(300.0, s.approachMs);
    double p = (now - (tNote - total)) / total;

    if (p < 0.0 || now > tNote + okWindow) {
        return;
    }

    float prog = float(std::clamp(p, 0.0, 1.0));
    float fadeOut = 1.f;
    if (now > tNote) {
        fadeOut = float(1.0 - std::clamp((now - tNote) / okWindow, 0.0, 1.0));
    }

    float radius = RING + (1.f - prog) * (SPAWN_R - RING);
    float ang = n.angle;
    float x = CX + ox + std::cos(ang) * radius;
    float y = CY + oy + std::sin(ang) * radius;

    float size = 5.f + 13.f * easeOutCubic(prog);
    float a = (0.25f + 0.75f * prog) * fadeOut;
    Color key = (n.key == 0) ? st.accent : st.accent2;
    Color core = key;
    float wob = std::sin(float(now) * 0.004f + float(idx)) * 0.06f;

    // Хвост наружу.
    for (int k = 1; k <= 6; ++k) {
        float d = float(k) * (5.f + 5.f * prog);
        float aa = a * (1.f - float(k) / 7.f) * 0.5f;
        g.line(x + std::cos(ang + wob) * d, y + std::sin(ang + wob) * d,
               x + std::cos(ang + wob) * (d - 4.f),
               y + std::sin(ang + wob) * (d - 4.f), 2.f,
               fade(key, aa));
    }

    // Камень.
    std::vector<Pt> pts;
    pts.reserve(9);
    float rot = float(now) * 0.0006f + float(idx) * 1.7f;
    for (int k = 0; k < 8; ++k) {
        float aa = rot + kTau * float(k) / 8.f;
        float rr = size * (0.72f + 0.28f * hash01(int(idx) * 9 + k));
        pts.push_back({x + std::cos(aa) * rr, y + std::sin(aa) * rr});
    }
    if (st.hardShadow > 0.2f) {
        std::vector<Pt> sh = pts;
        for (Pt &q : sh) {
            q.x += 4.f;
            q.y += 4.f;
        }
        g.polygon(sh, fade(hex(0x000000), 0.5f * a));
    }
    g.polygon(pts, fade(core, 0.92f * a));
    g.circle(x, y, size * 0.40f, fade(mix(core, st.bgTop, 0.35f), a), 20);
    g.ring(x, y, size * 0.44f, 1.4f, fade(contrastOn(core, st.text), 0.55f * a));
    g.glowCircle(x, y, size * 2.4f, key, 0.18f * a);
}

} // namespace

int State::resolvedNotes() const {
    int n = 0;
    for (size_t i = 0; i < hit.size(); ++i) {
        if (hit[i] != 0) {
            ++n;
        }
    }
    return n;
}

double State::accuracy() const {
    int resolved = counts[kPerfect] + counts[kGood] + counts[kOk] +
                   counts[kMiss];
    if (resolved <= 0) {
        return 1.0;
    }
    double sum = 300.0 * counts[kPerfect] + 150.0 * counts[kGood] +
                 60.0 * counts[kOk];
    return sum / (300.0 * double(resolved));
}

void start(State &s, const std::vector<Spawn> &notes, double startMs,
           double bpm, double beatOffsetMs, const Settings &cfg) {
    s = State{};
    s.notes = notes;
    s.hit.assign(notes.size(), 0);
    s.judge.assign(notes.size(), kMiss);
    s.hitDelta.assign(notes.size(), 1e9f);
    s.startMs = startMs;
    s.bpm = (bpm > 1.0) ? bpm : 120.0;
    s.offsetBeatMs = beatOffsetMs;
    s.endMs = notes.empty() ? startMs : notes.back().timeMs;
    s.arcHalf = (cfg.timing == 0) ? 0.62f : ((cfg.timing == 2) ? 0.42f : 0.52f);
    s.approachMs = double(cfg.approachSec) * 1000.0;
    s.running = true;
    s.started = true;
    s.arcAngle = -kPi * 0.5f;
    s.targetAngle = s.arcAngle;
}

void update(State &s, double songMs, float dt, const Settings &cfg) {
    s.songMs = songMs;
    s.offsetMs = cfg.offsetMs;
    s.approachMs = double(cfg.approachSec) * 1000.0;
    double now = songMs - double(cfg.offsetMs);

    double perfect = 45, good = 95, ok = 160;
    cfg.windows(perfect, good, ok);

    // Дуга плавно догоняет курсор — без рывков, но без заметной задержки.
    float k = 1.f - std::exp(-dt * 32.f);
    s.arcAngle = wrapAngle(s.arcAngle + angDiff(s.targetAngle, s.arcAngle) * k);

    // Промахи: метеорит пролетел мимо.
    for (size_t i = s.alive; i < s.notes.size(); ++i) {
        if (now > s.notes[i].timeMs + ok && s.hit[i] == 0) {
            s.hit[i] = 2;
            s.judge[i] = kMiss;
            s.counts[kMiss]++;
            s.combo = 0;
            s.lastJudge = kMiss;
            s.lastDelta = now - s.notes[i].timeMs;
            s.judgeT = 0.f;
            if (cfg.screenShake) {
                s.shake = std::max(s.shake, 0.55f);
            }
        }
    }
    while (s.alive < s.notes.size() && s.hit[s.alive] != 0) {
        ++s.alive;
    }

    for (Burst &b : s.bursts) {
        b.t += dt * 2.2f;
    }
    s.bursts.erase(std::remove_if(s.bursts.begin(), s.bursts.end(),
                                  [](const Burst &b) { return b.t >= 1.f; }),
                   s.bursts.end());

    s.judgeT += dt;
    s.shake = std::max(0.f, s.shake - dt * 1.6f);
    s.flash = std::max(0.f, s.flash - dt * 3.2f);
    for (float &k2 : s.keyGlow) {
        k2 = std::max(0.f, k2 - dt * 5.f);
    }

    if (s.alive >= s.notes.size() &&
        now > (s.notes.empty() ? s.startMs : s.notes.back().timeMs) + 1800.0) {
        s.finished = true;
    }
}

void aimAt(State &s, float x, float y) {
    float dx = x - CX;
    float dy = y - CY;
    if (dx * dx + dy * dy < 24.f * 24.f) {
        return;
    }
    s.targetAngle = std::atan2(dy, dx);
}

bool press(State &s, int key, const Settings &cfg) {
    if (key < 0 || key > 1) {
        return false;
    }
    s.keyGlow[key] = 1.f;

    double now = s.songMs - double(cfg.offsetMs);
    double perfect = 45, good = 95, ok = 160;
    cfg.windows(perfect, good, ok);

    int best = -1;
    double bestDt = 1e9;
    size_t last = std::min(s.notes.size(), s.alive + 24);
    for (size_t i = s.alive; i < last; ++i) {
        if (s.hit[i] != 0 || s.notes[i].key != key) {
            continue;
        }
        double dt = now - s.notes[i].timeMs;
        if (std::fabs(dt) > ok) {
            continue;
        }
        if (std::fabs(angDiff(s.notes[i].angle, s.arcAngle)) > s.arcHalf + 0.04f) {
            continue;
        }
        if (std::fabs(dt) < std::fabs(bestDt)) {
            bestDt = dt;
            best = int(i);
        }
    }

    if (best < 0) {
        return false;
    }

    int j = kMiss;
    double ad = std::fabs(bestDt);
    if (ad <= perfect) {
        j = kPerfect;
    } else if (ad <= good) {
        j = kGood;
    } else {
        j = kOk;
    }

    s.hit[size_t(best)] = 1;
    s.judge[size_t(best)] = (unsigned char)j;
    s.hitDelta[size_t(best)] = float(bestDt);
    s.counts[j]++;
    s.combo++;
    s.maxCombo = std::max(s.maxCombo, s.combo);
    int value = (j == kPerfect) ? 300 : ((j == kGood) ? 150 : 60);
    s.score += value + std::min(s.combo, 120) * 2;
    s.lastJudge = j;
    s.lastDelta = bestDt;
    s.judgeT = 0.f;
    s.flash = (j == kPerfect) ? 1.f : 0.6f;
    if (cfg.screenShake && j == kPerfect) {
        s.shake = std::max(s.shake, 0.18f);
    }

    s.bursts.push_back({s.notes[size_t(best)].angle, 0.f, key});
    while (s.alive < s.notes.size() && s.hit[s.alive] != 0) {
        ++s.alive;
    }
    return true;
}

void draw(Gfx &g, const Style &st, State &s, const Settings &cfg) {
    (void)cfg;
    float beats = 0.f;
    if (s.bpm > 1.0) {
        double b = (s.songMs - s.offsetBeatMs) / 60000.0 * s.bpm;
        float ph = float(b - std::floor(b));
        beats = std::pow(std::max(0.f, std::sin(ph * kPi)), 10.f);
    }

    stage(g, st, s, beats);

    float ox = 0.f, oy = 0.f;
    if (s.shake > 0.001f) {
        ox = std::sin(float(s.songMs) * 0.09f) * 7.f * s.shake;
        oy = std::cos(float(s.songMs) * 0.07f) * 5.f * s.shake;
    }

    double perfect = 45, good = 95, ok = 160;
    cfg.windows(perfect, good, ok);
    double now = s.songMs - double(s.offsetMs);

    ringAndArc(g, st, s, ox, oy);

    // Метеориты рисуем с дальних к ближним.
    for (size_t i = s.alive; i < s.notes.size(); ++i) {
        if (s.hit[i] == 0) {
            meteor(g, st, s, s.notes[i], i, ox, oy, now, ok);
        }
    }

    // Вспышки от попаданий.
    for (const Burst &b : s.bursts) {
        float t = clamp01(b.t);
        float ease = easeOutCubic(t);
        Color c = (b.key == 0) ? st.accent : st.accent2;
        for (int k = 0; k < 8; ++k) {
            float aa = b.angle + kTau * float(k) / 8.f + 0.2f * t;
            float r0 = RING + ease * 18.f;
            float r1 = RING + ease * (46.f + 26.f * hash01(k + 3));
            g.line(CX + ox + std::cos(aa) * r0, CY + oy + std::sin(aa) * r0,
                   CX + ox + std::cos(aa) * r1, CY + oy + std::sin(aa) * r1,
                   3.f * (1.f - t), fade(c, 0.75f * (1.f - t)));
        }
        g.ring(CX + ox, CY + oy, RING + 6.f + ease * 34.f, 2.f,
               fade(c, 0.55f * (1.f - t)));
    }

    if (s.flash > 0.01f) {
        g.glowCircle(CX + ox, CY + oy, 260.f, st.accent, 0.16f * s.flash);
    }

    // --- HUD --------------------------------------------------------------
    char buf[128];
    g.text("SCORE", 9, fade(st.dim, 0.65f), 64.f, 34.f, Gfx::Left, 3.f, false);
    std::snprintf(buf, sizeof buf, "%07d", s.score);
    g.text(buf, 26, fade(st.text, 0.95f), 64.f, 48.f, Gfx::Left, 1.5f, true);

    std::snprintf(buf, sizeof buf, "%.2f%%", s.accuracy() * 100.0);
    g.text("ACCURACY", 9, fade(st.dim, 0.65f), W - 64.f, 34.f, Gfx::Right, 3.f,
           false);
    g.text(buf, 22, fade(st.text, 0.92f), W - 64.f, 46.f, Gfx::Right, 1.2f,
           true);

    // Прогресс песни.
    float prog = 0.f;
    if (s.endMs > s.startMs) {
        prog = float(std::clamp((s.songMs - s.startMs) /
                                    (s.endMs - s.startMs),
                                0.0, 1.0));
    }
    g.fillRect({64.f, 92.f, W - 128.f, 2.f}, fade(st.text, 0.10f));
    g.fillRect({64.f, 92.f, (W - 128.f) * prog, 2.f}, fade(st.accent, 0.75f));

    // Калибровочная шкала: последнее попадание и средний сдвиг.
    {
        float y = 114.f, halfW = 150.f;
        double mean = 0.0;
        int cnt = 0;
        for (float d : s.hitDelta) {
            if (std::fabs(d) < 1e8f) {
                mean += d;
                ++cnt;
            }
        }
        if (cnt > 0) {
            mean /= cnt;
        }
        g.fillRect({CX - halfW, y, halfW * 2.f, 1.f}, fade(st.text, 0.13f));
        for (int k2 = -1; k2 <= 1; ++k2) {
            float x = CX + float(k2) * halfW;
            g.fillRect({x, y - 3.f, 1.f, 7.f},
                       fade(st.text, k2 == 0 ? 0.35f : 0.18f));
        }
        if (s.lastJudge >= 0 && s.lastJudge != kMiss) {
            float t = float(std::clamp(s.lastDelta / ok, -1.0, 1.0));
            float x = CX + t * halfW;
            g.fillRect({x - 1.f, y - 6.f, 2.f, 13.f},
                       fade(judgeColor(st, s.lastJudge), 0.9f));
        }
        if (cnt > 0) {
            float x = CX + float(std::clamp(mean / ok, -1.0, 1.0)) * halfW;
            g.fillRect({x, y - 9.f, 1.f, 19.f}, fade(st.accent2, 0.7f));
            std::snprintf(buf, sizeof buf, "AVG %+.0f MS", mean);
            g.text(buf, 8, fade(st.dim, 0.55f), CX, y + 7.f, Gfx::Center,
                   1.8f, false);
        }
        g.text("EARLY", 7, fade(st.dim, 0.32f), CX - halfW, y + 7.f,
               Gfx::Left, 1.6f, false);
        g.text("LATE", 7, fade(st.dim, 0.32f), CX + halfW, y + 7.f,
               Gfx::Right, 1.6f, false);
    }

    if (cfg.autoplay) {
        float pulse = 0.6f + 0.4f * std::sin(float(s.songMs) * 0.004f);
        g.text("AUTOPLAY", 8, fade(st.accent2, pulse * 0.9f), CX, 66.f,
               Gfx::Center, 3.2f, true);
    }

    // Комбо.
    if (s.combo > 1) {
        std::snprintf(buf, sizeof buf, "%d", s.combo);
        float th = g.textHeight(40, true);
        g.text(buf, 40, fade(st.text, 0.9f), CX, CY + RING + 22.f, Gfx::Center,
               1.f, true);
        g.text("COMBO", 9, fade(st.dim, 0.6f), CX,
               CY + RING + 26.f + th, Gfx::Center, 3.f, false);
    }

    // Оценка попадания.
    if (s.lastJudge >= 0 && s.judgeT < 1.1f) {
        float a = 1.f - clamp01((s.judgeT - 0.55f) / 0.55f);
        float pop = 1.f + 0.25f * std::exp(-s.judgeT * 14.f);
        Color c = judgeColor(st, s.lastJudge);
        std::string label = judgeName(s.lastJudge);
        if (s.lastJudge != kMiss) {
            std::snprintf(buf, sizeof buf, "%+.0f ms", s.lastDelta);
            g.text(buf, 11, fade(st.dim, 0.8f * a), CX, CY - 92.f,
                   Gfx::Center, 1.5f, false);
        }
        g.text(label, int(30.f * pop), fade(c, a * 0.95f), CX, CY - 74.f,
               Gfx::Center, 4.f, true);
    }

    // Название карты.
    if (!s.title.empty()) {
        g.text(s.title, 12, fade(st.text, 0.7f), 64.f, H - 62.f, Gfx::Left,
               1.2f, true);
        g.text(s.diff, 10, fade(st.dim, 0.55f), 64.f, H - 44.f, Gfx::Left,
               1.5f, false);
    }

    // Клавиши и подсказка, если метеорит уже в дуге.
    const char *labels[2] = {"Z", "X"};
    Settings::presetLabels(cfg.keyPreset, labels[0], labels[1]);

    bool armed[2] = {false, false};
    for (size_t i = s.alive; i < s.notes.size(); ++i) {
        if (s.hit[i] != 0) {
            continue;
        }
        double dt = now - s.notes[i].timeMs;
        if (std::fabs(dt) <= ok + 70.0 &&
            std::fabs(angDiff(s.notes[i].angle, s.arcAngle)) <= s.arcHalf) {
            armed[s.notes[i].key] = true;
        }
    }

    for (int k = 0; k < 2; ++k) {
        float x = CX + float(k * 2 - 1) * 58.f;
        float y = H - 58.f;
        Color c = (k == 0) ? st.accent : st.accent2;
        float gAlpha = std::max(s.keyGlow[k], armed[k] ? 0.55f : 0.f);
        if (gAlpha > 0.01f) {
            g.glowCircle(x, y, 34.f, c, 0.28f * gAlpha);
        }
        g.fillRoundRect({x - 24.f, y - 20.f, 48.f, 40.f}, 10.f, 0.f,
                        fade(mix(st.btnFill, c, armed[k] ? 0.55f : 0.08f),
                             (0.75f + 0.25f * armed[k]) * 0.95f));
        g.ring(x, y, 24.f, 1.f, fade(c, 0.35f + 0.5f * armed[k]));
        Color tc = contrastOn(mix(st.btnFill, c, armed[k] ? 0.55f : 0.08f),
                              st.text);
        float th = g.textHeight(15, true);
        g.text(labels[k], 15, fade(tc, 0.95f), x, y - th * 0.5f, Gfx::Center,
               1.f, true);
    }

    g.text("ESC — PAUSE · R — RESTART · ROTATE THE RING · HIT WITH YOUR KEY",
           9, fade(st.dim, 0.42f), CX, H - 22.f, Gfx::Center, 2.f, false);
}

} // namespace game