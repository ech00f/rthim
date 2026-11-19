#include "song.hpp"

#include <SDL3/SDL.h>
#include <mpg123.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace song {
namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;
constexpr double kFramesPerMs = double(kRate) / 1000.0; // 48
constexpr double kMsPerFrame = 1000.0 / double(kRate);  // 1/48
constexpr float kTau = 6.28318530718f;
constexpr int kFadeFrames = 240; // 5 мс мягкого старта после seek/resume
constexpr int kSfxVoices = 4;

// ---------------------------------------------------------------------------
// Общее состояние. Аудио-поток читает его без блокировок, изменения
// транспорта выполняются под SDL_LockAudioStream.
// ---------------------------------------------------------------------------

std::vector<float> g_pcm; // interleaved stereo, 48 кГц

SDL_AudioStream *g_stream = nullptr;

std::atomic<bool> g_loaded{false};
std::atomic<uint64_t> g_totalFrames{0};  // всего кадров песни
std::atomic<uint64_t> g_baseFrame{0};    // кадр, с которого начат текущий отрезок
std::atomic<uint64_t> g_readFrame{0};    // следующий кадр для чтения
std::atomic<uint64_t> g_pushedFrames{0}; // сколько кадров песни отдано в поток
std::atomic<bool> g_playing{false};
std::atomic<bool> g_paused{false};
std::atomic<bool> g_finished{false};
std::atomic<double> g_pausePosMs{0.0};
std::atomic<int> g_fadeRemaining{0};

std::atomic<float> g_musicVol{0.8f};
std::atomic<float> g_sfxVol{0.8f};
std::atomic<uint32_t> g_sfxReq[kSfxVoices]{};

// Состояние только аудио-потока.
std::vector<float> g_cbBuf;
float g_musicCur = 0.f;
float g_sfxCur = 0.f;
uint32_t g_sfxSeen[kSfxVoices] = {};

struct Voice {
    bool active = false;
    bool noise = false;
    float phase = 0.f;
    float phaseInc = 0.f;
    float env = 0.f;
    float decay = 0.f;
    int age = 0;
    float amp = 0.f;
    float lp = 0.f;
};

Voice g_voices[kSfxVoices];
uint32_t g_rngState = 0x51ED270Bu;

float rngNoise() {
    g_rngState ^= g_rngState << 13;
    g_rngState ^= g_rngState >> 17;
    g_rngState ^= g_rngState << 5;
    return float(g_rngState) * (1.f / 2147483648.f) - 1.f;
}

float voiceSample(Voice &v) {
    if (!v.active) {
        return 0.f;
    }
    float s;
    if (v.noise) {
        // Низкий глухой шум: белый шум через однополюсный ФНЧ.
        v.lp += (rngNoise() - v.lp) * 0.035f;
        s = v.lp * 2.6f;
    } else {
        s = std::sin(v.phase) * v.amp;
        v.phase += v.phaseInc;
        if (v.phase >= kTau) {
            v.phase -= kTau;
        }
    }
    v.env *= v.decay;
    ++v.age;
    float attack = v.age < 72 ? float(v.age) / 72.f : 1.f;
    s *= v.env * attack;
    if (v.env < 0.0004f) {
        v.active = false;
    }
    return s;
}

void startVoice(int kind) {
    if (kind < 0 || kind >= kSfxVoices) {
        return;
    }
    Voice &v = g_voices[kind];
    v.active = true;
    v.noise = kind == 2;
    v.phase = 0.f;
    v.phaseInc = (kind == 0 ? 880.f
                            : (kind == 1 ? 1320.f : (kind == 3 ? 1760.f : 220.f))) *
                 kTau / float(kRate);
    v.env = 1.f;
    // Затухание: слышимая длительность около 100 мс.
    const float tau = kind == 2 ? 0.045f : (kind == 3 ? 0.018f : 0.028f);
    v.decay = std::exp(-1.f / (float(kRate) * tau));
    v.age = 0;
    v.amp = kind == 0 ? 0.5f
                      : (kind == 1 ? 0.44f : (kind == 3 ? 0.22f : 0.42f));
    v.lp = 0.f;
}

float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

// ---------------------------------------------------------------------------
// Аудио-callback
// ---------------------------------------------------------------------------

void SDLCALL audioCallback(void *, SDL_AudioStream *stream, int additional,
                           int) {
    if (additional <= 0) {
        return;
    }
    const int frames = additional / int(sizeof(float) * kChannels);
    if (frames <= 0) {
        return;
    }

    const size_t need = size_t(frames) * kChannels;
    if (g_cbBuf.size() < need) {
        g_cbBuf.resize(need);
    }
    std::fill(g_cbBuf.begin(), g_cbBuf.begin() + ptrdiff_t(need), 0.f);

    // Заявки на звуки ударов: main только увеличивает счётчик.
    for (int k = 0; k < kSfxVoices; ++k) {
        const uint32_t req = g_sfxReq[k].load(std::memory_order_relaxed);
        if (req != g_sfxSeen[k]) {
            g_sfxSeen[k] = req;
            startVoice(k);
        }
    }

    const bool playMusic = g_playing.load(std::memory_order_relaxed) &&
                           !g_paused.load(std::memory_order_relaxed) &&
                           !g_finished.load(std::memory_order_relaxed);

    uint64_t read = g_readFrame.load(std::memory_order_relaxed);
    const uint64_t total = g_totalFrames.load(std::memory_order_relaxed);
    int musicFrames = 0;
    if (playMusic && read < total) {
        const uint64_t avail = total - read;
        musicFrames = int(std::min<uint64_t>(avail, uint64_t(frames)));
    }

    const float *pcm = g_pcm.empty() ? nullptr : g_pcm.data();
    int fade = g_fadeRemaining.load(std::memory_order_relaxed);
    const float musicTarget = g_musicVol.load(std::memory_order_relaxed);
    const float sfxTarget = g_sfxVol.load(std::memory_order_relaxed);
    const float smooth = 1.f - std::exp(-1.f / (float(kRate) * 0.008f));

    for (int i = 0; i < frames; ++i) {
        g_musicCur += (musicTarget - g_musicCur) * smooth;
        g_sfxCur += (sfxTarget - g_sfxCur) * smooth;

        float l = 0.f;
        float r = 0.f;

        if (i < musicFrames && pcm) {
            float gain = g_musicCur;
            if (fade > 0) {
                gain *= float(kFadeFrames - fade + 1) / float(kFadeFrames);
                --fade;
            }
            const size_t idx = (size_t(read) + size_t(i)) * kChannels;
            l += pcm[idx] * gain;
            r += pcm[idx + 1] * gain;
        }

        if (g_sfxCur > 0.0001f) {
            float s = 0.f;
            for (Voice &v : g_voices) {
                s += voiceSample(v);
            }
            l += s * g_sfxCur;
            r += s * g_sfxCur;
        } else {
            // Голоса всё равно должны «дотикивать», иначе после прибавления
            // громкости они оживут с середины.
            for (Voice &v : g_voices) {
                voiceSample(v);
            }
        }

        g_cbBuf[size_t(i) * kChannels] = std::clamp(l, -1.f, 1.f);
        g_cbBuf[size_t(i) * kChannels + 1] = std::clamp(r, -1.f, 1.f);
    }

    if (musicFrames > 0) {
        read += uint64_t(musicFrames);
        g_readFrame.store(read, std::memory_order_relaxed);
        g_pushedFrames.fetch_add(uint64_t(musicFrames),
                                 std::memory_order_relaxed);
        if (read >= total) {
            g_playing.store(false, std::memory_order_relaxed);
            g_finished.store(true, std::memory_order_relaxed);
        }
    }
    g_fadeRemaining.store(fade, std::memory_order_relaxed);

    SDL_PutAudioStreamData(stream, g_cbBuf.data(),
                           int(need * sizeof(float)));
}

// ---------------------------------------------------------------------------
// Поток вывода
// ---------------------------------------------------------------------------

bool ensureStream(std::string &err) {
    if (g_stream) {
        return true;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        err = std::string("SDL audio init: ") + SDL_GetError();
        return false;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = kChannels;
    spec.freq = kRate;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, audioCallback, nullptr);
    if (!g_stream) {
        err = std::string("SDL audio stream: ") + SDL_GetError();
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(g_stream)) {
        err = std::string("SDL audio resume: ") + SDL_GetError();
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Разбор WAV
// ---------------------------------------------------------------------------

uint16_t le16(const unsigned char *p) {
    return uint16_t(uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8));
}

uint32_t le32(const unsigned char *p) {
    return uint32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                    (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
}

float wavSample(const unsigned char *p, uint16_t format, uint16_t bits) {
    if (format == 3) { // IEEE float
        if (bits == 32) {
            float v = 0.f;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
        if (bits == 64) {
            double v = 0.0;
            std::memcpy(&v, p, sizeof(v));
            return float(v);
        }
        return 0.f;
    }
    switch (bits) {
    case 8:
        return (float(p[0]) - 128.f) / 128.f;
    case 16: {
        const int16_t v = int16_t(le16(p));
        return float(v) / 32768.f;
    }
    case 24: {
        uint32_t u = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                     (uint32_t(p[2]) << 16);
        if (u & 0x800000u) {
            u |= 0xFF000000u;
        }
        int32_t v = 0;
        std::memcpy(&v, &u, sizeof(v));
        return float(v) / 8388608.f;
    }
    case 32: {
        const uint32_t u = le32(p);
        int32_t v = 0;
        std::memcpy(&v, &u, sizeof(v));
        return float(float(v) / 2147483648.f);
    }
    default:
        return 0.f;
    }
}

// Линейная интерполяция в 48 кГц, stereo float на выходе.
void resampleTo48(const std::vector<float> &in, int srcRate, int srcCh,
                  std::vector<float> &out) {
    if (srcCh < 1) {
        srcCh = 1;
    }
    if (srcRate <= 0) {
        srcRate = kRate;
    }
    const size_t srcFrames = in.size() / size_t(srcCh);
    if (srcFrames == 0) {
        out.clear();
        return;
    }
    if (srcRate == kRate && srcCh == kChannels) {
        out = in;
        return;
    }

    size_t outFrames = size_t(std::llround(double(srcFrames) * double(kRate) /
                                           double(srcRate)));
    if (outFrames == 0) {
        outFrames = 1;
    }
    out.resize(outFrames * kChannels);

    const double step = double(srcRate) / double(kRate);
    const size_t last = srcFrames - 1;
    for (size_t n = 0; n < outFrames; ++n) {
        const double sp = double(n) * step;
        size_t i0 = size_t(sp);
        double frac = sp - double(i0);
        if (i0 > last) {
            i0 = last;
            frac = 0.0;
        }
        const size_t i1 = i0 < last ? i0 + 1 : last;
        for (int c = 0; c < kChannels; ++c) {
            const int sc = srcCh == 1 ? 0 : (c < srcCh ? c : srcCh - 1);
            const float a = in[i0 * size_t(srcCh) + size_t(sc)];
            const float b = in[i1 * size_t(srcCh) + size_t(sc)];
            out[n * kChannels + size_t(c)] =
                float(double(a) + (double(b) - double(a)) * frac);
        }
    }
}

bool decodeWav(const std::vector<unsigned char> &d, std::vector<float> &out,
               std::string &err) {
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 ||
        std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        err = "WAV: нет заголовка RIFF/WAVE";
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t blockAlign = 0;
    uint16_t bits = 0;
    const unsigned char *data = nullptr;
    size_t dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const unsigned char *id = d.data() + pos;
        const uint32_t size = le32(d.data() + pos + 4);
        const size_t body = pos + 8;
        const size_t avail = d.size() - body;
        const size_t real = std::min<size_t>(size_t(size), avail);

        if (std::memcmp(id, "fmt ", 4) == 0 && real >= 16) {
            format = le16(d.data() + body);
            channels = le16(d.data() + body + 2);
            rate = le32(d.data() + body + 4);
            blockAlign = le16(d.data() + body + 12);
            bits = le16(d.data() + body + 14);
            // WAVE_FORMAT_EXTENSIBLE: настоящий формат в SubFormat.
            if (format == 0xFFFE && real >= 40) {
                format = le16(d.data() + body + 24);
            }
        } else if (std::memcmp(id, "data", 4) == 0) {
            data = d.data() + body;
            dataSize = real;
        }

        if (size_t(size) > avail) {
            break; // обрезанный файл, но уже прочитанное используем
        }
        // Чанки выровнены по чётной границе.
        pos = body + size_t(size) + size_t(size & 1u);
    }

    if (!data || dataSize == 0 || channels == 0 || rate == 0 || bits == 0) {
        err = "WAV: нет чанков fmt/data";
        return false;
    }
    if (bits % 8 != 0 || (format != 1 && format != 3)) {
        err = "WAV: неподдерживаемый формат сэмплов";
        return false;
    }

    const size_t bytesPerSample = size_t(bits) / 8;
    const size_t frameBytes = size_t(channels) * bytesPerSample;
    const size_t stride = blockAlign >= frameBytes ? size_t(blockAlign)
                                                   : frameBytes;
    if (stride == 0 || frameBytes == 0) {
        err = "WAV: некорректный block align";
        return false;
    }
    const size_t frames = dataSize / stride;
    if (frames == 0) {
        err = "WAV: нет сэмплов";
        return false;
    }

    std::vector<float> src(frames * size_t(kChannels));
    for (size_t f = 0; f < frames; ++f) {
        const unsigned char *p = data + f * stride;
        const float l = wavSample(p, format, bits);
        float r = l;
        if (channels > 1) {
            r = wavSample(p + bytesPerSample, format, bits);
        }
        src[f * 2] = l;
        src[f * 2 + 1] = r;
    }

    resampleTo48(src, int(rate), kChannels, out);
    return !out.empty();
}

// ---------------------------------------------------------------------------
// Разбор MP3 (mpg123, feed-режим без временных файлов)
// ---------------------------------------------------------------------------

std::once_flag g_mpgOnce;
bool g_mpgReady = false;

bool ensureMpg123() {
    std::call_once(g_mpgOnce, [] { g_mpgReady = mpg123_init() == MPG123_OK; });
    return g_mpgReady;
}

bool decodeMp3(const std::vector<unsigned char> &bytes,
               std::vector<float> &out, std::string &err) {
    if (!ensureMpg123()) {
        err = "MP3: mpg123_init не удался";
        return false;
    }
    if (bytes.empty()) {
        err = "MP3: пустой файл";
        return false;
    }

    mpg123_handle *h = mpg123_new(nullptr, nullptr);
    if (!h) {
        err = "MP3: mpg123_new не удался";
        return false;
    }

    auto fail = [&](const char *msg) {
        mpg123_delete(h);
        err = msg;
        return false;
    };

    if (mpg123_format_none(h) != MPG123_OK) {
        return fail("MP3: mpg123_format_none");
    }
    // MP3 бывает на любой из стандартных частот; просим float32 везде,
    // ресемплим в 48 кГц уже сами.
    const long rates[] = {8000,  11025, 12000, 16000, 22050,
                          24000, 32000, 44100, 48000};
    for (long rate : rates) {
        if (mpg123_format(h, rate, MPG123_MONO | MPG123_STEREO,
                          MPG123_ENC_FLOAT_32) != MPG123_OK) {
            return fail("MP3: mpg123_format");
        }
    }
    if (mpg123_open_feed(h) != MPG123_OK) {
        return fail("MP3: mpg123_open_feed");
    }
    if (mpg123_feed(h, bytes.data(), bytes.size()) != MPG123_OK) {
        return fail("MP3: mpg123_feed");
    }

    std::vector<float> raw;
    int srcRate = 0;
    int srcChannels = 0;
    bool gotData = false;

    alignas(16) float buf[8192];
    for (;;) {
        size_t done = 0;
        const int ret = mpg123_read(h, buf, sizeof(buf), &done);
        if (done > 0) {
            const size_t n = done / sizeof(float);
            raw.insert(raw.end(), buf, buf + n);
            gotData = true;
        }
        if (ret == MPG123_NEW_FORMAT) {
            long rate = 0;
            int channels = 0;
            int enc = 0;
            if (mpg123_getformat(h, &rate, &channels, &enc) != MPG123_OK ||
                rate <= 0 || channels <= 0) {
                return fail("MP3: mpg123_getformat");
            }
            if (enc != MPG123_ENC_FLOAT_32) {
                return fail("MP3: mpg123 не дал float32");
            }
            if (srcRate != 0 && (rate != srcRate || channels != srcChannels)) {
                return fail("MP3: переменный формат потока не поддержан");
            }
            srcRate = int(rate);
            srcChannels = channels;
            continue;
        }
        if (ret == MPG123_OK) {
            continue;
        }
        if (ret == MPG123_DONE || ret == MPG123_NEED_MORE) {
            break;
        }
        return fail("MP3: ошибка декодирования");
    }
    mpg123_delete(h);

    if (!gotData || raw.empty() || srcRate <= 0 || srcChannels <= 0) {
        err = "MP3: не удалось декодировать сэмплы";
        return false;
    }

    resampleTo48(raw, srcRate, srcChannels, out);
    return !out.empty();
}

// ---------------------------------------------------------------------------
// Файлы
// ---------------------------------------------------------------------------

bool readFile(const std::string &path, std::vector<unsigned char> &bytes,
              std::string &err) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "не открыть файл: " + path;
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        err = "не прочитать файл: " + path;
        return false;
    }
    const long len = std::ftell(f);
    if (len <= 0) {
        std::fclose(f);
        err = "пустой файл: " + path;
        return false;
    }
    std::rewind(f);
    bytes.resize(size_t(len));
    const size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size()) {
        bytes.clear();
        err = "файл прочитан не полностью: " + path;
        return false;
    }
    return true;
}

double clampMs(double v, double dur) {
    if (!(v > 0.0)) {
        return 0.0; // заодно съедает NaN
    }
    return v < dur ? v : dur;
}

} // namespace

// ---------------------------------------------------------------------------
// Загрузка
// ---------------------------------------------------------------------------

bool load(const std::string &path, std::string &err) {
    err.clear();
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes, err)) {
        return false;
    }
    return loadFromMemory(bytes, path, err);
}

bool loadFromMemory(const std::vector<unsigned char> &bytes,
                    const std::string &nameHint, std::string &err) {
    err.clear();
    std::vector<float> pcm;

    const bool isWav = bytes.size() >= 12 &&
                       std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
                       std::memcmp(bytes.data() + 8, "WAVE", 4) == 0;
    bool ok = false;
    if (isWav) {
        ok = decodeWav(bytes, pcm, err);
    } else {
        ok = decodeMp3(bytes, pcm, err);
    }
    if (!ok) {
        if (err.empty()) {
            err = "не удалось декодировать: " + nameHint;
        }
        return false;
    }

    for (float &x : pcm) {
        if (!std::isfinite(x)) {
            x = 0.f;
        } else {
            // MP3 после декодирования может «звенеть» чуть выше 1.0;
            // в потоке всё равно нужен диапазон float.
            x = std::clamp(x, -1.f, 1.f);
        }
    }

    unload();
    if (!ensureStream(err)) {
        return false;
    }

    SDL_LockAudioStream(g_stream);
    g_pcm = std::move(pcm);
    g_totalFrames.store(uint64_t(g_pcm.size() / kChannels),
                        std::memory_order_relaxed);
    g_baseFrame.store(0, std::memory_order_relaxed);
    g_readFrame.store(0, std::memory_order_relaxed);
    g_pushedFrames.store(0, std::memory_order_relaxed);
    g_finished.store(false, std::memory_order_relaxed);
    g_paused.store(false, std::memory_order_relaxed);
    g_playing.store(false, std::memory_order_relaxed);
    g_pausePosMs.store(0.0, std::memory_order_relaxed);
    g_fadeRemaining.store(0, std::memory_order_relaxed);
    // Не проигрывать заявки, накопившиеся до загрузки.
    for (int k = 0; k < kSfxVoices; ++k) {
        g_sfxSeen[k] = g_sfxReq[k].load(std::memory_order_relaxed);
        g_voices[k].active = false;
    }
    g_loaded.store(true, std::memory_order_relaxed);
    SDL_UnlockAudioStream(g_stream);
    return true;
}

void unload() {
    if (g_stream) {
        SDL_LockAudioStream(g_stream);
        g_loaded.store(false, std::memory_order_relaxed);
        g_playing.store(false, std::memory_order_relaxed);
        g_paused.store(false, std::memory_order_relaxed);
        g_finished.store(false, std::memory_order_relaxed);
        g_totalFrames.store(0, std::memory_order_relaxed);
        g_baseFrame.store(0, std::memory_order_relaxed);
        g_readFrame.store(0, std::memory_order_relaxed);
        g_pushedFrames.store(0, std::memory_order_relaxed);
        g_pausePosMs.store(0.0, std::memory_order_relaxed);
        SDL_ClearAudioStream(g_stream);
        SDL_UnlockAudioStream(g_stream);
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
    } else {
        g_loaded.store(false, std::memory_order_relaxed);
        g_totalFrames.store(0, std::memory_order_relaxed);
        g_playing.store(false, std::memory_order_relaxed);
        g_paused.store(false, std::memory_order_relaxed);
        g_finished.store(false, std::memory_order_relaxed);
        g_pausePosMs.store(0.0, std::memory_order_relaxed);
        g_fadeRemaining.store(0, std::memory_order_relaxed);
    }
    std::vector<float>().swap(g_pcm);
    for (Voice &v : g_voices) {
        v.active = false;
    }
}

// ---------------------------------------------------------------------------
// Транспорт
// ---------------------------------------------------------------------------

void play(double fromMs) {
    if (!g_loaded.load(std::memory_order_relaxed) || !g_stream) {
        return;
    }
    const double dur = durationMs();
    const double pos = clampMs(fromMs, dur);
    const uint64_t total = g_totalFrames.load(std::memory_order_relaxed);
    uint64_t frame = uint64_t(pos * kFramesPerMs + 0.5);
    if (frame > total) {
        frame = total;
    }

    SDL_LockAudioStream(g_stream);
    g_playing.store(false, std::memory_order_relaxed);
    g_paused.store(true, std::memory_order_relaxed);
    SDL_ClearAudioStream(g_stream);
    g_baseFrame.store(frame, std::memory_order_relaxed);
    g_readFrame.store(frame, std::memory_order_relaxed);
    g_pushedFrames.store(0, std::memory_order_relaxed);
    g_finished.store(false, std::memory_order_relaxed);
    g_pausePosMs.store(double(frame) * kMsPerFrame, std::memory_order_relaxed);
    g_fadeRemaining.store(kFadeFrames, std::memory_order_relaxed);
    g_paused.store(false, std::memory_order_relaxed);
    g_playing.store(true, std::memory_order_relaxed);
    SDL_UnlockAudioStream(g_stream);
    SDL_ResumeAudioStreamDevice(g_stream);
}

void pause() {
    if (!g_stream || !g_loaded.load(std::memory_order_relaxed)) {
        return;
    }
    SDL_LockAudioStream(g_stream);
    if (!g_playing.load(std::memory_order_relaxed) ||
        g_paused.load(std::memory_order_relaxed)) {
        SDL_UnlockAudioStream(g_stream);
        return;
    }

    const uint64_t base = g_baseFrame.load(std::memory_order_relaxed);
    const uint64_t pushed = g_pushedFrames.load(std::memory_order_relaxed);
    int queued = SDL_GetAudioStreamQueued(g_stream);
    if (queued < 0) {
        queued = 0;
    }
    const double queuedFrames = double(queued) / (sizeof(float) * kChannels);
    double frame = double(base) + double(pushed) - queuedFrames;
    const double dur = durationMs();
    const double ms = clampMs(frame * kMsPerFrame, dur);

    const uint64_t frozen = uint64_t(ms * kFramesPerMs + 0.5);
    g_paused.store(true, std::memory_order_relaxed);
    g_pausePosMs.store(double(frozen) * kMsPerFrame, std::memory_order_relaxed);
    g_baseFrame.store(frozen, std::memory_order_relaxed);
    g_readFrame.store(frozen, std::memory_order_relaxed);
    g_pushedFrames.store(0, std::memory_order_relaxed);
    SDL_ClearAudioStream(g_stream);
    SDL_UnlockAudioStream(g_stream);
}

void resume() {
    if (!g_stream || !g_loaded.load(std::memory_order_relaxed)) {
        return;
    }
    SDL_LockAudioStream(g_stream);
    if (!g_playing.load(std::memory_order_relaxed) ||
        !g_paused.load(std::memory_order_relaxed)) {
        SDL_UnlockAudioStream(g_stream);
        return;
    }
    SDL_ClearAudioStream(g_stream);
    g_pushedFrames.store(0, std::memory_order_relaxed);
    g_fadeRemaining.store(kFadeFrames, std::memory_order_relaxed);
    g_paused.store(false, std::memory_order_relaxed);
    SDL_UnlockAudioStream(g_stream);
    SDL_ResumeAudioStreamDevice(g_stream);
}

void stop() {
    if (!g_stream) {
        g_playing.store(false, std::memory_order_relaxed);
        g_paused.store(false, std::memory_order_relaxed);
        g_finished.store(false, std::memory_order_relaxed);
        return;
    }
    SDL_LockAudioStream(g_stream);
    g_playing.store(false, std::memory_order_relaxed);
    g_paused.store(false, std::memory_order_relaxed);
    g_finished.store(false, std::memory_order_relaxed);
    g_baseFrame.store(0, std::memory_order_relaxed);
    g_readFrame.store(0, std::memory_order_relaxed);
    g_pushedFrames.store(0, std::memory_order_relaxed);
    g_pausePosMs.store(0.0, std::memory_order_relaxed);
    g_fadeRemaining.store(0, std::memory_order_relaxed);
    SDL_ClearAudioStream(g_stream);
    SDL_UnlockAudioStream(g_stream);
}

bool loaded() {
    return g_loaded.load(std::memory_order_relaxed);
}

bool playing() {
    return g_loaded.load(std::memory_order_relaxed) &&
           g_playing.load(std::memory_order_relaxed) &&
           !g_paused.load(std::memory_order_relaxed) &&
           !g_finished.load(std::memory_order_relaxed);
}

bool paused() {
    return g_loaded.load(std::memory_order_relaxed) &&
           g_playing.load(std::memory_order_relaxed) &&
           g_paused.load(std::memory_order_relaxed);
}

bool finished() {
    return g_finished.load(std::memory_order_relaxed);
}

double positionMs() {
    if (!g_loaded.load(std::memory_order_relaxed)) {
        return 0.0;
    }
    const double dur = durationMs();
    if (g_finished.load(std::memory_order_relaxed)) {
        return dur;
    }
    if (g_paused.load(std::memory_order_relaxed)) {
        return clampMs(g_pausePosMs.load(std::memory_order_relaxed), dur);
    }
    if (!g_playing.load(std::memory_order_relaxed) || !g_stream) {
        return 0.0;
    }

    const uint64_t base = g_baseFrame.load(std::memory_order_relaxed);
    const uint64_t pushed = g_pushedFrames.load(std::memory_order_relaxed);
    int queued = SDL_GetAudioStreamQueued(g_stream);
    if (queued < 0) {
        queued = 0;
    }
    const double queuedFrames = double(queued) / (sizeof(float) * kChannels);
    const double frames = double(base) + double(pushed) - queuedFrames;
    return clampMs(frames * kMsPerFrame, dur);
}

double durationMs() {
    const uint64_t frames = g_totalFrames.load(std::memory_order_relaxed);
    return double(frames) * kMsPerFrame;
}

// ---------------------------------------------------------------------------
// Громкости и звуки
// ---------------------------------------------------------------------------

void setMusicVolume(float v) {
    g_musicVol.store(clamp01(v), std::memory_order_relaxed);
}

void setSfxVolume(float v) {
    g_sfxVol.store(clamp01(v), std::memory_order_relaxed);
}

float musicVolume() {
    return g_musicVol.load(std::memory_order_relaxed);
}

float sfxVolume() {
    return g_sfxVol.load(std::memory_order_relaxed);
}

void sfx(int kind) {
    if (kind < 0 || kind >= kSfxVoices) {
        return;
    }
    g_sfxReq[kind].fetch_add(1, std::memory_order_release);
}

} // namespace song