#pragma once

#include "beatmap.hpp"
#include "gfx.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// Список карт: .osz в songs/ и папки с .osu (в том числе самодельные).
namespace mapdb {

struct Entry {
    beatmap::Map map;
    std::string sourcePath; // .osz или .osu
    std::string audioPath;  // заполняется лениво
    std::string coverPath;
    std::string id;         // стабильный ключ кеша
    uint64_t hash = 0;
    double lengthMs = 0.0;
    int objects = 0;
    bool custom = false;
    bool audioReady = false;

    SDL_Texture *cover = nullptr;
    int coverW = 0, coverH = 0;

    std::string title() const { return map.displayTitle(); }
    std::string artist() const { return map.displayArtist(); }
};

struct DB {
    std::vector<Entry> entries;

    void scan();
    bool ensureAudio(Entry &e, std::string &err);
    bool ensureCover(Entry &e, Gfx &g);
    void dropCover(Entry &e);
    void dropAllCovers();

    int count() const { return int(entries.size()); }
};

std::string cacheDirFor(uint64_t hash);

} // namespace mapdb