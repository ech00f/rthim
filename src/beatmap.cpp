#include "beatmap.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

#include <zlib.h>

namespace beatmap {

namespace {

constexpr size_t kMaxFileSize = 512u * 1024u * 1024u; // 512 МБ — предохранитель
constexpr uint32_t kSigEOCD = 0x06054b50;            // PK\x05\x06
constexpr uint32_t kSigCentral = 0x02014b50;         // PK\x01\x02
constexpr uint32_t kSigLocal = 0x04034b50;           // PK\x03\x04

uint16_t rd16(const unsigned char *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rd32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::string lowerAscii(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool endsWithNoCase(const std::string &s, const std::string &suffix) {
    if (s.size() < suffix.size())
        return false;
    return lowerAscii(s.substr(s.size() - suffix.size())) == lowerAscii(suffix);
}

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
        --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

bool parseDouble(const std::string &s, double &out) {
    const char *b = s.data();
    const char *e = b + s.size();
    while (b < e && (*b == ' ' || *b == '\t'))
        ++b;
    double v = 0.0;
    auto r = std::from_chars(b, e, v);
    if (r.ec != std::errc() || r.ptr == b)
        return false;
    out = v;
    return true;
}

bool parseInt(const std::string &s, long long &out) {
    const char *b = s.data();
    const char *e = b + s.size();
    while (b < e && (*b == ' ' || *b == '\t'))
        ++b;
    long long v = 0;
    auto r = std::from_chars(b, e, v);
    if (r.ec != std::errc() || r.ptr == b)
        return false;
    out = v;
    return true;
}

bool readWholeFile(const std::string &path, std::vector<unsigned char> &out,
                   std::string &err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "не открыть файл: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len < 0) {
        err = "не определить размер: " + path;
        return false;
    }
    if (static_cast<uint64_t>(len) > kMaxFileSize) {
        err = "файл слишком большой";
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(len));
    if (len > 0 && !f.read(reinterpret_cast<char *>(out.data()), len)) {
        err = "не прочитать файл: " + path;
        return false;
    }
    return true;
}

struct ZipEntry {
    std::string name;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localOff = 0;
};

// Разбирает central directory. ZIP64 и шифрование не поддерживаются.
bool parseCentralDirectory(const std::vector<unsigned char> &z,
                           std::vector<ZipEntry> &entries, std::string &err) {
    if (z.size() < 22) {
        err = "файл не похож на zip";
        return false;
    }

    // EOCD ищем в последних 64 КБ + 22 байта.
    size_t last = z.size() - 22;
    size_t first = z.size() > 65557 ? z.size() - 65557 : 0;
    size_t eocd = std::numeric_limits<size_t>::max();
    for (size_t i = last + 1; i-- > first;) {
        if (rd32(&z[i]) == kSigEOCD) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::numeric_limits<size_t>::max()) {
        err = "не найден конец zip-архива";
        return false;
    }
    if (eocd + 22 > z.size()) {
        err = "повреждён конец zip-архива";
        return false;
    }

    uint32_t total = rd16(&z[eocd + 10]);
    uint32_t cdSize = rd32(&z[eocd + 12]);
    uint32_t cdOff = rd32(&z[eocd + 16]);
    if (total == 0xFFFFu || cdOff == 0xFFFFFFFFu || cdSize == 0xFFFFFFFFu) {
        err = "ZIP64 не поддерживается";
        return false;
    }
    if (total > 100000u) {
        err = "слишком много файлов в архиве";
        return false;
    }
    if (static_cast<uint64_t>(cdOff) + cdSize > z.size()) {
        err = "таблица файлов выходит за пределы архива";
        return false;
    }

    entries.clear();
    entries.reserve(total);
    size_t p = cdOff;
    for (uint32_t i = 0; i < total; ++i) {
        if (p + 46 > z.size() || rd32(&z[p]) != kSigCentral) {
            err = "повреждён список файлов архива";
            return false;
        }
        ZipEntry e;
        e.flags = rd16(&z[p + 8]);
        e.method = rd16(&z[p + 10]);
        e.compSize = rd32(&z[p + 20]);
        e.uncompSize = rd32(&z[p + 24]);
        uint16_t nameLen = rd16(&z[p + 28]);
        uint16_t extraLen = rd16(&z[p + 30]);
        uint16_t commentLen = rd16(&z[p + 32]);
        e.localOff = rd32(&z[p + 42]);

        size_t next = p + 46;
        if (next + nameLen + extraLen + commentLen > z.size()) {
            err = "имя файла выходит за пределы архива";
            return false;
        }
        e.name.assign(reinterpret_cast<const char *>(&z[next]), nameLen);

        if (e.compSize == 0xFFFFFFFFu || e.uncompSize == 0xFFFFFFFFu ||
            e.localOff == 0xFFFFFFFFu) {
            err = "ZIP64 не поддерживается";
            return false;
        }
        if (e.uncompSize > kMaxFileSize) {
            err = "файл в архиве слишком большой";
            return false;
        }
        entries.push_back(std::move(e));
        p = next + nameLen + extraLen + commentLen;
    }
    return true;
}

const ZipEntry *findEntry(const std::vector<ZipEntry> &entries,
                          const std::string &name) {
    for (const ZipEntry &e : entries)
        if (e.name == name)
            return &e;
    // Запасной вариант: регистронезависимое сравнение (имена в zip могут
    // приходить в другой раскладке, чем в .osu).
    std::string want = lowerAscii(name);
    for (const ZipEntry &e : entries)
        if (lowerAscii(e.name) == want)
            return &e;
    return nullptr;
}

bool inflateEntry(const std::vector<unsigned char> &z, const ZipEntry &e,
                  std::vector<unsigned char> &out, std::string &err) {
    if (e.flags & 1u) {
        err = "зашифрованные файлы не поддерживаются";
        return false;
    }
    if (e.method != 0 && e.method != 8) {
        err = "неподдерживаемый метод сжатия";
        return false;
    }

    size_t lo = e.localOff;
    if (lo + 30 > z.size() || rd32(&z[lo]) != kSigLocal) {
        err = "повреждён локальный заголовок файла";
        return false;
    }
    uint16_t nameLen = rd16(&z[lo + 26]);
    uint16_t extraLen = rd16(&z[lo + 28]);
    size_t dataOff = lo + 30;
    if (dataOff + nameLen + extraLen > z.size()) {
        err = "заголовок файла выходит за пределы архива";
        return false;
    }
    dataOff += nameLen + extraLen;
    if (dataOff + e.compSize > z.size()) {
        err = "данные файла выходят за пределы архива";
        return false;
    }

    if (e.uncompSize == 0) {
        out.clear();
        return true;
    }

    if (e.method == 0) {
        if (e.compSize < e.uncompSize) {
            err = "размер несжатого файла не совпадает";
            return false;
        }
        out.assign(z.begin() + static_cast<std::ptrdiff_t>(dataOff),
                   z.begin() + static_cast<std::ptrdiff_t>(dataOff + e.uncompSize));
        return true;
    }

    out.resize(e.uncompSize);
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) {
        err = "не удалось инициализировать zlib";
        return false;
    }
    s.next_in = const_cast<Bytef *>(&z[dataOff]);
    s.avail_in = e.compSize;
    s.next_out = out.data();
    s.avail_out = e.uncompSize;

    int r = inflate(&s, Z_FINISH);
    bool ok = (r == Z_STREAM_END) && s.total_out == e.uncompSize;
    inflateEnd(&s);
    if (!ok) {
        err = "повреждённые данные внутри архива";
        return false;
    }
    return true;
}

bool isImageName(const std::string &name) {
    return endsWithNoCase(name, ".png") || endsWithNoCase(name, ".jpg") ||
           endsWithNoCase(name, ".jpeg") || endsWithNoCase(name, ".bmp") ||
           endsWithNoCase(name, ".gif") || endsWithNoCase(name, ".webp");
}

// Достаёт имя файла из строки [Events]: 0,0,"bg.png",0,0
std::string eventFileName(const std::string &line) {
    size_t q1 = line.find('"');
    if (q1 != std::string::npos) {
        size_t q2 = line.find('"', q1 + 1);
        if (q2 != std::string::npos)
            return line.substr(q1 + 1, q2 - q1 - 1);
    }
    // Без кавычек: третий элемент (после двух запятых).
    std::vector<std::string> parts = split(line, ',');
    if (parts.size() >= 3)
        return trim(parts[2]);
    return std::string();
}

double effectiveBeatLen(const std::vector<TimingPoint> &timing, double time,
                        double baseBeatLen) {
    double base = 0.0;
    double sv = 0.0;
    bool haveSv = false;
    for (const TimingPoint &tp : timing) {
        if (tp.timeMs > time + 0.5)
            break;
        if (!tp.inherited) {
            base = tp.beatLenMs;
            haveSv = false;
        } else {
            sv = tp.beatLenMs;
            haveSv = true;
        }
    }
    if (base <= 0.0)
        base = baseBeatLen;
    if (base <= 0.0)
        base = 500.0;
    if (haveSv && sv < 0.0) {
        double vel = -100.0 / sv;
        if (vel < 0.1)
            vel = 0.1;
        if (vel > 10.0)
            vel = 10.0;
        base /= vel;
    }
    return base;
}

bool parseOsuText(const std::string &text, Map &m, std::string &err) {
    m = Map{};
    std::string src = text;
    // UTF-8 BOM.
    if (src.size() >= 3 && static_cast<unsigned char>(src[0]) == 0xEF &&
        static_cast<unsigned char>(src[1]) == 0xBB &&
        static_cast<unsigned char>(src[2]) == 0xBF) {
        src.erase(0, 3);
    }
    if (src.empty()) {
        err = "пустой файл .osu";
        return false;
    }

    std::string section;
    std::vector<std::string> rawObjects;
    double sliderMultiplier = 1.0;

    size_t pos = 0;
    while (pos < src.size()) {
        size_t nl = src.find('\n', pos);
        std::string line = src.substr(pos, nl == std::string::npos
                                               ? std::string::npos
                                               : nl - pos);
        pos = (nl == std::string::npos) ? src.size() : nl + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::string t = trim(line);
        if (t.empty())
            continue;
        if (t.rfind("//", 0) == 0)
            continue;

        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }

        if (section == "General" || section == "Metadata" ||
            section == "Difficulty") {
            size_t colon = t.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = trim(t.substr(0, colon));
            std::string val = trim(t.substr(colon + 1));

            if (section == "General") {
                if (key == "AudioFilename")
                    m.audioFile = val;
                else if (key == "Mode") {
                    long long v = 0;
                    if (parseInt(val, v))
                        m.mode = static_cast<int>(v);
                }
            } else if (section == "Metadata") {
                if (key == "Title")
                    m.title = val;
                else if (key == "TitleUnicode")
                    m.titleUnicode = val;
                else if (key == "Artist")
                    m.artist = val;
                else if (key == "ArtistUnicode")
                    m.artistUnicode = val;
                else if (key == "Creator")
                    m.creator = val;
                else if (key == "Version")
                    m.version = val;
                else if (key == "Source")
                    m.source = val;
                else if (key == "Tags")
                    m.tags = val;
            } else {
                double v = 0.0;
                bool ok = parseDouble(val, v);
                if (key == "HPDrainRate" && ok)
                    m.hp = static_cast<float>(v);
                else if (key == "OverallDifficulty" && ok)
                    m.od = static_cast<float>(v);
                else if (key == "CircleSize" && ok)
                    m.cs = static_cast<float>(v);
                else if (key == "ApproachRate" && ok)
                    m.ar = static_cast<float>(v);
                else if (key == "SliderMultiplier" && ok)
                    sliderMultiplier = v;
            }
            continue;
        }

        if (section == "Events") {
            if (m.bgFile.empty()) {
                std::vector<std::string> parts = split(t, ',');
                if (parts.size() >= 2 && trim(parts[0]) == "0") {
                    std::string name = eventFileName(t);
                    if (!name.empty() && isImageName(name))
                        m.bgFile = name;
                }
                // Тип 1 — видео, его пропускаем; старый формат "Video,..." тоже.
            }
            continue;
        }

        if (section == "TimingPoints") {
            std::vector<std::string> p = split(t, ',');
            if (p.size() < 2)
                continue;
            TimingPoint tp;
            if (!parseDouble(trim(p[0]), tp.timeMs))
                continue;
            if (!parseDouble(trim(p[1]), tp.beatLenMs))
                continue;
            if (p.size() >= 7) {
                long long uninherited = 0;
                if (parseInt(trim(p[6]), uninherited))
                    tp.inherited = (uninherited == 0);
                else
                    tp.inherited = tp.beatLenMs < 0.0;
            } else {
                tp.inherited = tp.beatLenMs < 0.0;
            }
            m.timing.push_back(tp);
            continue;
        }

        if (section == "HitObjects") {
            rawObjects.push_back(t);
            continue;
        }
    }

    std::stable_sort(m.timing.begin(), m.timing.end(),
                     [](const TimingPoint &a, const TimingPoint &b) {
                         return a.timeMs < b.timeMs;
                     });

    // BPM и offset — по таймингпоинтам, а не по файловому порядку.
    if (!m.timing.empty())
        m.offsetMs = m.timing.front().timeMs;
    bool haveBpm = false;
    for (const TimingPoint &tp : m.timing) {
        if (!tp.inherited && tp.beatLenMs > 0.0) {
            m.bpm = 60000.0 / tp.beatLenMs;
            haveBpm = true;
            break;
        }
    }
    if (!haveBpm) {
        m.bpm = 120.0;
        m.offsetMs = m.timing.empty() ? 0.0 : m.timing.front().timeMs;
    }

    double baseBeatLen = 500.0;
    for (const TimingPoint &tp : m.timing)
        if (!tp.inherited && tp.beatLenMs > 0.0) {
            baseBeatLen = tp.beatLenMs;
            break;
        }

    int comboCounter = 0;
    for (const std::string &line : rawObjects) {
        std::vector<std::string> p = split(line, ',');
        if (p.size() < 4)
            continue;
        double x = 0.0, y = 0.0, time = 0.0;
        long long rawType = 0;
        if (!parseDouble(trim(p[0]), x) || !parseDouble(trim(p[1]), y) ||
            !parseDouble(trim(p[2]), time) || !parseInt(trim(p[3]), rawType))
            continue;

        int typeBits = static_cast<int>(rawType);
        HitObject obj;
        obj.x = static_cast<float>(x);
        obj.y = static_cast<float>(y);
        obj.timeMs = time;
        obj.endMs = time;

        if (typeBits & 8) {
            obj.type = 2; // spinner
            if (p.size() >= 6) {
                double end = 0.0;
                if (parseDouble(trim(p[5]), end) && end > obj.endMs)
                    obj.endMs = end;
            }
        } else if (typeBits & 2) {
            obj.type = 1; // slider
            double repeats = 1.0;
            double length = 0.0;
            bool haveLen = false;
            if (p.size() >= 7) {
                double v = 0.0;
                if (parseDouble(trim(p[6]), v) && v >= 1.0)
                    repeats = v;
            }
            if (p.size() >= 8) {
                double v = 0.0;
                if (parseDouble(trim(p[7]), v) && v >= 0.0) {
                    length = v;
                    haveLen = true;
                }
            }
            if (haveLen && sliderMultiplier > 0.0) {
                double beat =
                    effectiveBeatLen(m.timing, time, baseBeatLen);
                double dur = length / (sliderMultiplier * 100.0) * beat * repeats;
                if (dur > 0.0 && std::isfinite(dur))
                    obj.endMs = time + dur;
            }
        } else {
            obj.type = 0; // circle
        }

        if (typeBits & 4) {
            obj.combo = comboCounter;
            ++comboCounter;
        } else {
            obj.combo = comboCounter;
        }

        double a = std::atan2(y - 192.0, x - 256.0);
        constexpr double kPi = 3.14159265358979323846;
        if (a < -kPi || a > kPi) {
            a = std::fmod(a + kPi, 2.0 * kPi);
            if (a < 0.0)
                a += 2.0 * kPi;
            a -= kPi;
        }
        obj.angle = static_cast<float>(a);

        m.objects.push_back(obj);
    }

    std::stable_sort(m.objects.begin(), m.objects.end(),
                     [](const HitObject &a, const HitObject &b) {
                         return a.timeMs < b.timeMs;
                     });

    m.valid = true;
    return true;
}

bool loadZip(const std::string &path, std::vector<unsigned char> &z,
             std::vector<ZipEntry> &entries, std::string &err) {
    if (!readWholeFile(path, z, err))
        return false;
    return parseCentralDirectory(z, entries, err);
}

} // namespace

const std::string &Map::displayTitle() const {
    return titleUnicode.empty() ? title : titleUnicode;
}

const std::string &Map::displayArtist() const {
    return artistUnicode.empty() ? artist : artistUnicode;
}

bool loadOsz(const std::string &path, Map &out, std::string &err) {
    try {
        out = Map{};
        std::vector<unsigned char> z;
        std::vector<ZipEntry> entries;
        if (!loadZip(path, z, entries, err))
            return false;

        std::vector<const ZipEntry *> osuEntries;
        for (const ZipEntry &e : entries)
            if (endsWithNoCase(e.name, ".osu"))
                osuEntries.push_back(&e);
        if (osuEntries.empty()) {
            err = "в архиве нет .osu";
            return false;
        }

        bool haveFallback = false;
        bool haveChosen = false;
        Map fallback;
        Map chosen;
        for (const ZipEntry *e : osuEntries) {
            std::vector<unsigned char> data;
            std::string e2;
            if (!inflateEntry(z, *e, data, e2))
                continue;
            std::string text(data.begin(), data.end());
            Map m;
            std::string perr;
            if (!parseOsuText(text, m, perr))
                continue;
            if (!haveFallback) {
                fallback = m;
                haveFallback = true;
            }
            if (m.mode == 0) {
                chosen = std::move(m);
                haveChosen = true;
                break; // первый .osu с Mode: 0
            }
        }

        if (!haveFallback) {
            err = "не удалось разобрать ни один .osu в архиве";
            return false;
        }
        out = haveChosen ? std::move(chosen) : std::move(fallback);
        if (!out.valid) {
            err = "не удалось разобрать карту";
            return false;
        }

        out.oszPath = path;
        out.osuPath.clear();
        err.clear();
        return true;
    } catch (...) {
        err = "внутренняя ошибка при чтении .osz";
        return false;
    }
}

bool loadOsu(const std::string &path, Map &out, std::string &err) {
    try {
        out = Map{};
        std::vector<unsigned char> data;
        if (!readWholeFile(path, data, err))
            return false;
        std::string text(data.begin(), data.end());
        if (!parseOsuText(text, out, err))
            return false;
        out.oszPath.clear();
        out.osuPath = path;
        err.clear();
        return true;
    } catch (...) {
        err = "внутренняя ошибка при чтении .osu";
        return false;
    }
}

bool readEntry(const std::string &oszPath, const std::string &entry,
               std::vector<unsigned char> &out, std::string &err) {
    try {
        out.clear();
        std::vector<unsigned char> z;
        std::vector<ZipEntry> entries;
        if (!loadZip(oszPath, z, entries, err))
            return false;
        const ZipEntry *e = findEntry(entries, entry);
        if (!e) {
            err = "файл не найден в архиве: " + entry;
            return false;
        }
        if (!inflateEntry(z, *e, out, err))
            return false;
        err.clear();
        return true;
    } catch (...) {
        err = "внутренняя ошибка при чтении архива";
        return false;
    }
}

bool extractTo(const std::string &oszPath, const std::string &entry,
               const std::string &dstPath, std::string &err) {
    try {
        std::vector<unsigned char> data;
        if (!readEntry(oszPath, entry, data, err))
            return false;

        std::filesystem::path dst(dstPath);
        std::error_code ec;
        if (dst.has_parent_path()) {
            const std::filesystem::path &parent = dst.parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, ec);
        }
        if (ec) {
            err = "не создать папку для файла: " + dst.parent_path().string();
            return false;
        }

        std::ofstream f(dst, std::ios::binary | std::ios::trunc);
        if (!f) {
            err = "не открыть файл для записи: " + dstPath;
            return false;
        }
        if (!data.empty())
            f.write(reinterpret_cast<const char *>(data.data()),
                    static_cast<std::streamsize>(data.size()));
        if (!f) {
            err = "не записать файл: " + dstPath;
            return false;
        }
        err.clear();
        return true;
    } catch (...) {
        err = "внутренняя ошибка при извлечении файла";
        return false;
    }
}

} // namespace beatmap