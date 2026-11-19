#pragma once

#include <string>
#include <vector>

namespace beatmap {

// Один объект карты. Времена — в миллисекундах от начала аудио.
struct HitObject {
    double timeMs = 0;      // начало
    double endMs = 0;       // конец для слайдеров/спиннеров (иначе == timeMs)
    float x = 256, y = 192; // координаты osu! 512x384
    int type = 0;           // 0 circle, 1 slider, 2 spinner
    int combo = 0;          // номер комбо (для цвета)
    float angle = 0;        // atan2(y-192, x-256), радианы, [-pi,pi]
};

// Точка тайминга. Для inherited beatLenMs — отрицательное значение из файла.
struct TimingPoint {
    double timeMs = 0;
    double beatLenMs = 500;
    bool inherited = false;
};

struct Map {
    std::string oszPath;     // путь к .osz (пусто для папки)
    std::string osuPath;     // путь к .osu (для папки/кеша)
    std::string title, titleUnicode, artist, artistUnicode;
    std::string creator, version, source, tags;
    std::string audioFile;   // имя аудио внутри архива/папки
    std::string bgFile;      // имя фона (может быть пусто)
    double bpm = 120.0;      // по первому не-inherited таймингпоинту
    double offsetMs = 0.0;   // время первого таймингпоинта
    int mode = 0;
    float hp = 5, od = 5, cs = 4, ar = 5;
    std::vector<TimingPoint> timing;
    std::vector<HitObject> objects; // отсортированы по timeMs
    bool valid = false;

    const std::string &displayTitle() const; // titleUnicode если не пуст, иначе title
    const std::string &displayArtist() const;
};

// Открыть .osz: читает zip, парсит первый .osu с Mode: 0 (если есть), заполняет Map.
bool loadOsz(const std::string &path, Map &out, std::string &err);

// Открыть одиночный .osu (для самодельных карт): заполняет Map, osuPath = path.
bool loadOsu(const std::string &path, Map &out, std::string &err);

// Достать один файл из .osz в память.
bool readEntry(const std::string &oszPath, const std::string &entry,
               std::vector<unsigned char> &out, std::string &err);

// Записать файл из .osz на диск (создать родительские папки).
bool extractTo(const std::string &oszPath, const std::string &entry,
               const std::string &dstPath, std::string &err);

} // namespace beatmap