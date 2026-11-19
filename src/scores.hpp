#pragma once

#include <string>

// Лучшие результаты по картам. Ключ — стабильный id карты (хеш пути),
// поэтому результаты переживают пересканирование и переименование папок.
struct ScoreEntry {
    bool valid = false;
    int score = 0;
    double acc = 0.0;
    int maxCombo = 0;
    int plays = 0;
    long long lastPlayed = 0;
    int counts[4] = {};
};

namespace scores {

void load(const std::string &path);
void save(const std::string &path);

const ScoreEntry *get(const std::string &id);

// Записать результат. best — итоговая лучшая запись, before — что было до
// неё. Возвращает true, если это новый рекорд по очкам.
bool submit(const std::string &id, int score, double acc, int combo,
            const int counts[4], ScoreEntry &best, ScoreEntry &before);

} // namespace scores