#include "audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace audio {

// Атомики публикуются из аудиопотока и читаются рендером.
std::atomic<float> g_weights[3] = {{1.f}, {0.f}, {0.f}};
std::atomic<float> g_pulse{0.f};
std::atomic<float> g_low{0.f};
std::atomic<float> g_mid{0.f};
std::atomic<float> g_high{0.f};
std::atomic<float> g_beatPhase{0.f};
std::atomic<int> g_beatIndex{0};
std::atomic<int> g_bpm{128};

namespace {

constexpr int kRate = 48000;
constexpr float kTau = 6.28318530718f;
constexpr int kBpm[3] = {128, 96, 88};

inline float noteHz(int midi) {
    return 440.f * std::pow(2.f, float(midi - 69) / 12.f);
}

inline float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

// Детерминированный шум: без него барабаны звучат как синтезатор из 80-х.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    float uni() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(s) * (1.f / 4294967296.f);
    }
    float noise() { return uni() * 2.f - 1.f; }
};

struct Layer {
    // бочка
    float kPh = 0.f, kEnv = 0.f, kClick = 0.f;
    // малый / рим
    float sPh = 0.f, sEnv = 0.f, sDecay = 0.999f;
    // хэт / шейкер
    float hEnv = 0.f, hPrev = 0.f, hDecay = 0.999f;
    // бас
    float bPh = 0.f, bEnv = 0.f, bLp = 0.f, bFreq = 55.f, bDecay = 0.999f;
    // мелодия
    float lPh = 0.f, lEnv = 0.f, lLp = 0.f, lFreq = 440.f, lDecay = 0.999f;
    // аккорд / пэд
    float padPh[3] = {};
    float padFreq[3] = {noteHz(53), noteHz(57), noteHz(60)};
    float padEnv = 0.f, padDecay = 1.f;
    // секвенсор
    int step = 0;
    double stepTime = 0.0;
};

// Коэффициенты затухания огибающих (на семпл).
float decayFor(float seconds) {
    return std::exp(-1.f / (kRate * std::max(0.001f, seconds)));
}

// Один луп = один мир. Паттерны на 16 шагов (16-е доли).
struct Mixer {
    Layer lay[3];
    Rng rng{0xA5A5F00Du};
    float gain[3] = {1.f, 0.f, 0.f};
    float offWeights[3] = {-1.f, -1.f, -1.f};

    float lpLow = 0.f, lpMid = 0.f, lpHigh = 0.f;
    float lvlLow = 0.f, lvlMid = 0.f, lvlHigh = 0.f;
    float pulse = 0.f;
    float crackle = 0.f;
    uint64_t counter = 0;

    void reset() {
        *this = Mixer{};
        lay[1].padEnv = 1.f;
        lay[1].padDecay = 1.f;
        lay[1].padFreq[0] = noteHz(53); // F3
        lay[1].padFreq[1] = noteHz(57); // A3
        lay[1].padFreq[2] = noteHz(60); // C4
        for (int i = 0; i < 3; ++i) {
            trigger(i, 0);
        }
    }

    void trigger(int idx, int step) {
        Layer &v = lay[idx];

        if (idx == 0) {
            // NEON: ровная четырёхдольная бочка, оффбит-хэт, злой бас, арп.
            if (step % 4 == 0) {
                v.kEnv = 1.f;
                v.kClick = 0.45f;
                v.kPh = 0.f;
            }
            if (step % 4 == 2) {
                v.hEnv = 0.42f;
                v.hDecay = decayFor(0.075f);
            }
            if (step == 4 || step == 12) {
                v.sEnv = 0.26f;
                v.sPh = 0.f;
                v.sDecay = decayFor(0.11f);
            }
            static const int bass[16] = {33, -1, -1, 33, -1, -1, 36, -1,
                                         31, -1, -1, 33, -1, -1, 40, -1};
            if (bass[step] > 0) {
                v.bFreq = noteHz(bass[step]);
                v.bEnv = 1.f;
                v.bLp = 0.f;
                v.bDecay = decayFor(0.15f);
            }
            static const int lead[16] = {57, -1, 60, -1, 64, -1, 67, -1,
                                         69, -1, 67, -1, 64, -1, 60, -1};
            if (lead[step] > 0) {
                v.lFreq = noteHz(lead[step]);
                v.lEnv = 0.75f;
                v.lLp = 0.f;
                v.lDecay = decayFor(0.11f);
            }
        } else if (idx == 1) {
            // BLOOM: половинная бочка, мягкий рим, шейкер, колокольчики.
            if (step == 0 || step == 8) {
                v.kEnv = 0.75f;
                v.kClick = 0.10f;
                v.kPh = 0.f;
            }
            if (step == 6 || step == 14) {
                v.kEnv = 0.35f;
                v.kClick = 0.04f;
                v.kPh = 0.f;
            }
            if (step == 4 || step == 12) {
                v.sEnv = 0.14f;
                v.sPh = 0.f;
                v.sDecay = decayFor(0.07f);
            }
            if (step % 2 == 0) {
                v.hEnv = 0.09f;
                v.hDecay = decayFor(0.035f);
            }
            if (step == 0) {
                v.bFreq = noteHz(29);
                v.bEnv = 1.f;
                v.bLp = 0.f;
                v.bDecay = decayFor(1.6f);
            }
            if (step == 8) {
                v.bFreq = noteHz(36);
                v.bEnv = 0.85f;
                v.bLp = 0.f;
                v.bDecay = decayFor(1.4f);
            }
            static const int bell[16] = {65, -1, -1, 69, -1, -1, 72, -1,
                                         -1, -1, 76, -1, 72, -1, 69, -1};
            if (bell[step] > 0) {
                v.lFreq = noteHz(bell[step]);
                v.lEnv = 0.5f;
                v.lDecay = decayFor(0.85f);
            }
        } else {
            // INK: бум-бэп, снейр на 2 и 4, хэты, контрабас, роудс.
            if (step == 0 || step == 6 || step == 10) {
                v.kEnv = 1.f;
                v.kClick = 0.30f;
                v.kPh = 0.f;
            }
            if (step == 3) {
                v.kEnv = 0.30f;
                v.kClick = 0.f;
                v.kPh = 0.f;
            }
            if (step == 4 || step == 12) {
                v.sEnv = 0.5f;
                v.sPh = 0.f;
                v.sDecay = decayFor(0.17f);
            }
            if (step % 2 == 0) {
                v.hEnv = (step % 4 == 0) ? 0.16f : 0.09f;
                v.hDecay = decayFor(0.045f);
            }
            static const int bass[16] = {38, -1, -1, -1, -1, -1, -1, -1,
                                         41, -1, 45, -1, -1, -1, 36, -1};
            if (bass[step] > 0) {
                v.bFreq = noteHz(bass[step]);
                v.bEnv = 1.f;
                v.bLp = 0.f;
                v.bDecay = decayFor(0.75f);
            }
            if (step == 4 || step == 12) {
                v.padEnv = 1.f;
                v.padDecay = decayFor(0.55f);
                v.padFreq[0] = noteHz(62); // D4
                v.padFreq[1] = noteHz(65); // F4
                v.padFreq[2] = noteHz(69); // A4
            }
        }
    }

    // Возвращает семпл лупа; kickOut — огибающая бочки для пульса.
    float styleSample(int idx, float &kickOut, float noise) {
        Layer &v = lay[idx];
        const double stepDur = 60.0 / double(kBpm[idx]) / 4.0;
        v.stepTime += 1.0 / kRate;
        if (v.stepTime >= stepDur) {
            v.stepTime -= stepDur;
            v.step = (v.step + 1) & 15;
            trigger(idx, v.step);
        }

        // бочка: синус с падением высоты + щелчок
        float kick = 0.f;
        if (v.kEnv > 0.0008f) {
            float f = 44.f + 150.f * v.kEnv * v.kEnv;
            v.kPh += f / kRate;
            v.kPh -= std::floor(v.kPh);
            kick = std::sin(v.kPh * kTau) * v.kEnv;
            kick += v.kClick * noise * 0.5f;
            v.kEnv *= decayFor(0.20f);
            v.kClick *= decayFor(0.004f);
        }
        kickOut = v.kEnv;

        // малый: шум + тон
        float snare = 0.f;
        if (v.sEnv > 0.0008f) {
            v.sPh += 182.f / kRate;
            v.sPh -= std::floor(v.sPh);
            snare = (noise * 0.72f + std::sin(v.sPh * kTau) * 0.34f) * v.sEnv;
            v.sEnv *= v.sDecay;
        }

        // хэт: разность шума — грубый хайпасс
        float hat = 0.f;
        {
            float hp = noise - v.hPrev;
            v.hPrev = noise;
            if (v.hEnv > 0.0008f) {
                hat = hp * v.hEnv;
                v.hEnv *= v.hDecay;
            }
        }

        // бас
        float bass = 0.f;
        if (v.bEnv > 0.0008f) {
            v.bPh += v.bFreq / kRate;
            v.bPh -= std::floor(v.bPh);
            float saw = 2.f * v.bPh - 1.f;
            v.bLp += (saw - v.bLp) * 0.18f;
            bass = v.bLp * v.bEnv;
            v.bEnv *= v.bDecay;
        }

        // мелодия: пила (NEON) или колокол/роудс (BLOOM/INK)
        float lead = 0.f;
        if (v.lEnv > 0.0008f) {
            v.lPh += v.lFreq / kRate;
            v.lPh -= std::floor(v.lPh);
            if (idx == 0) {
                float saw = 2.f * v.lPh - 1.f;
                v.lLp += (saw - v.lLp) * 0.22f;
                lead = v.lLp;
            } else {
                float p = v.lPh * kTau;
                lead = std::sin(p) + 0.32f * std::sin(p * 2.f) +
                       0.10f * std::sin(p * 3.f);
            }
            lead *= v.lEnv;
            v.lEnv *= v.lDecay;
        }

        // аккорд / пэд
        float pad = 0.f;
        if (v.padEnv > 0.0008f) {
            float s = 0.f;
            for (int k = 0; k < 3; ++k) {
                v.padPh[k] += v.padFreq[k] / kRate;
                v.padPh[k] -= std::floor(v.padPh[k]);
                s += std::sin(v.padPh[k] * kTau);
            }
            pad = s * (1.f / 3.f) * v.padEnv;
            if (idx == 1) {
                // у пэда BLOOM нет затухания — только медленное дыхание
                pad *= 0.7f + 0.3f * std::sin(float(counter) *
                                             kTau * 0.13f / kRate);
            } else {
                v.padEnv *= v.padDecay;
            }
        }

        switch (idx) {
        case 0:
            return kick * 0.85f + snare * 0.35f + hat * 0.26f +
                   bass * 0.55f + lead * 0.24f;
        case 1:
            return kick * 0.55f + snare * 0.20f + hat * 0.10f +
                   bass * 0.42f + lead * 0.30f + pad * 0.35f;
        default:
            return kick * 0.80f + snare * 0.48f + hat * 0.16f +
                   bass * 0.50f + lead * 0.16f + pad * 0.42f;
        }
    }

    void render(float *out, int frames) {
        const float aLow = 1.f - std::exp(-kTau * 120.f / kRate);
        const float aMid = 1.f - std::exp(-kTau * 2200.f / kRate);
        const float aHigh = 1.f - std::exp(-kTau * 5200.f / kRate);

        for (int i = 0; i < frames; ++i) {
            for (int k = 0; k < 3; ++k) {
                float target = (offWeights[0] >= 0.f)
                                   ? offWeights[k]
                                   : g_weights[k].load(std::memory_order_relaxed);
                gain[k] += (target - gain[k]) * 0.0018f;
            }

            float n = rng.noise();
            float mix = 0.f;
            float kickMax = 0.f;
            for (int k = 0; k < 3; ++k) {
                float kick = 0.f;
                float s = styleSample(k, kick, n);
                float g = std::sqrt(std::max(0.f, gain[k]));
                mix += s * g;
                kickMax = std::max(kickMax, kick * g);
            }

            // винил в INK: редкие щелчки + тихий шорох
            if (gain[2] > 0.01f) {
                if (rng.uni() < 0.0004f) {
                    crackle = 0.35f + 0.65f * rng.uni();
                }
                mix += (n * 0.05f + rng.noise() * crackle * 0.5f) *
                       std::sqrt(gain[2]) * 0.35f;
                crackle *= 0.986f;
            }

            mix = std::tanh(mix * 1.15f) * 0.62f;

            // уровни для визуала
            lpLow += (mix - lpLow) * aLow;
            lpMid += (mix - lpMid) * aMid;
            lpHigh += (mix - lpHigh) * aHigh;
            float bl = std::fabs(lpLow);
            float bm = std::fabs(lpMid - lpLow);
            float bh = std::fabs(mix - lpHigh);
            lvlLow += (bl - lvlLow) * (bl > lvlLow ? 0.20f : 0.0015f);
            lvlMid += (bm - lvlMid) * (bm > lvlMid ? 0.20f : 0.0018f);
            lvlHigh += (bh - lvlHigh) * (bh > lvlHigh ? 0.25f : 0.0025f);
            pulse += (kickMax - pulse) * (kickMax > pulse ? 0.35f : 0.06f);

            out[i * 2] = mix;
            out[i * 2 + 1] = mix;
            ++counter;

            if ((counter & 63) == 0) {
                publish();
            }
        }
        publish();
    }

    void publish() {
        g_pulse.store(clamp01(pulse * 1.15f), std::memory_order_relaxed);
        g_low.store(clamp01(lvlLow * 2.6f), std::memory_order_relaxed);
        g_mid.store(clamp01(lvlMid * 2.4f), std::memory_order_relaxed);
        g_high.store(clamp01(lvlHigh * 8.f), std::memory_order_relaxed);

        int best = 0;
        for (int k = 1; k < 3; ++k) {
            if (gain[k] > gain[best]) {
                best = k;
            }
        }
        const double stepDur = 60.0 / double(kBpm[best]) / 4.0;
        double within = lay[best].stepTime / stepDur;
        int beat = (lay[best].step >> 2) & 3;
        float phase = float((lay[best].step & 3) + within) * 0.25f;
        g_beatPhase.store(phase, std::memory_order_relaxed);
        g_beatIndex.store(beat, std::memory_order_relaxed);
        g_bpm.store(kBpm[best], std::memory_order_relaxed);
    }
};

Mixer g_mixer;
SDL_AudioStream *g_stream = nullptr;

} // namespace

namespace {

void SDLCALL audioCallback(void *, SDL_AudioStream *stream,
                           int additional, int) {
    if (additional <= 0) {
        return;
    }
    int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) {
        return;
    }
    static std::vector<float> buf;
    if (int(buf.size()) < frames * 2) {
        buf.resize(size_t(frames) * 2);
    }
    g_mixer.render(buf.data(), frames);
    SDL_PutAudioStreamData(stream, buf.data(), additional);
}

bool writeWav(const char *path, const std::vector<float> &pcm) {
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        return false;
    }
    uint32_t dataBytes = uint32_t(pcm.size() * 2);
    uint32_t rate = kRate;
    uint32_t byteRate = rate * 2 * 2;
    uint16_t blockAlign = 4;
    uint16_t bits = 16;
    uint32_t chunk = 36 + dataBytes;

    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f);
    w32(chunk);
    std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16(2);
    w32(rate);
    w32(byteRate);
    w16(blockAlign);
    w16(bits);
    std::fwrite("data", 1, 4, f);
    w32(dataBytes);
    for (float x : pcm) {
        int v = int(std::lround(std::clamp(x, -1.f, 1.f) * 32767.f));
        w16(uint16_t(int16_t(v)));
    }
    std::fclose(f);
    return true;
}

} // namespace

bool init() {
    if (g_stream) {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kRate;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, audioCallback, nullptr);
    if (!g_stream) {
        return false;
    }
    g_mixer.reset();
    if (!SDL_ResumeAudioStreamDevice(g_stream)) {
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
        return false;
    }
    return true;
}

void shutdown() {
    if (g_stream) {
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool running() { return g_stream != nullptr; }

void setWeights(float w0, float w1, float w2) {
    g_weights[0].store(std::max(0.f, w0), std::memory_order_relaxed);
    g_weights[1].store(std::max(0.f, w1), std::memory_order_relaxed);
    g_weights[2].store(std::max(0.f, w2), std::memory_order_relaxed);
}

Levels levels() {
    Levels l;
    l.pulse = g_pulse.load(std::memory_order_relaxed);
    l.low = g_low.load(std::memory_order_relaxed);
    l.mid = g_mid.load(std::memory_order_relaxed);
    l.high = g_high.load(std::memory_order_relaxed);
    l.beatPhase = g_beatPhase.load(std::memory_order_relaxed);
    l.beatIndex = g_beatIndex.load(std::memory_order_relaxed);
    l.bpm = g_bpm.load(std::memory_order_relaxed);
    return l;
}

int styleBpm(int index) {
    return kBpm[std::clamp(index, 0, 2)];
}

bool bounce(const char *dir, float seconds) {
    int frames = int(seconds * kRate);
    bool ok = true;
    for (int idx = 0; idx < 3; ++idx) {
        Mixer m;
        m.reset();
        m.offWeights[0] = (idx == 0) ? 1.f : 0.f;
        m.offWeights[1] = (idx == 1) ? 1.f : 0.f;
        m.offWeights[2] = (idx == 2) ? 1.f : 0.f;
        // прогрев: секвенсор должен дойти до начала такта
        m.gain[0] = m.offWeights[0];
        m.gain[1] = m.offWeights[1];
        m.gain[2] = m.offWeights[2];
        std::vector<float> pcm(size_t(frames) * 2);
        m.render(pcm.data(), frames);

        char path[512];
        std::snprintf(path, sizeof path, "%s/rthim_%s.wav", dir,
                      idx == 0 ? "neon" : (idx == 1 ? "bloom" : "ink"));
        Levels dbg = levels();
        std::printf("  %s levels: pulse=%.2f low=%.2f mid=%.2f high=%.2f\n",
                    idx == 0 ? "neon" : (idx == 1 ? "bloom" : "ink"),
                    dbg.pulse, dbg.low, dbg.mid, dbg.high);
        if (!writeWav(path, pcm)) {
            ok = false;
        } else {
            std::printf("saved %s\n", path);
        }
    }
    return ok;
}

} // namespace audio