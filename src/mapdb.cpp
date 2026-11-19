#include "mapdb.hpp"

#include "paths.hpp"

#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "../third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace mapdb {
namespace {

namespace fs = std::filesystem;

std::string lowerExt(const fs::path &p) {
    std::string e = p.extension().string();
    for (char &c : e) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

uint64_t hashPath(const std::string &p) {
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    std::string key = ec ? p : abs.string();
    return std::hash<std::string>{}(key);
}

std::string hexId(uint64_t h) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

} // namespace

std::string cacheDirFor(uint64_t hash) {
    return (fs::path(paths::cache()) / hexId(hash)).string();
}

void DB::dropCover(Entry &e) {
    if (e.cover) {
        SDL_DestroyTexture(e.cover);
        e.cover = nullptr;
    }
}

void DB::dropAllCovers() {
    for (Entry &e : entries) {
        dropCover(e);
    }
}

void DB::scan() {
    dropAllCovers();
    entries.clear();

    std::error_code ec;

    auto addMap = [&](const beatmap::Map &m, const std::string &source,
                      bool custom) {
        if (!m.valid || m.objects.empty()) {
            return;
        }
        Entry e;
        e.map = m;
        e.sourcePath = source;
        e.hash = hashPath(source);
        e.id = hexId(e.hash);
        e.custom = custom;
        e.objects = int(m.objects.size());
        e.lengthMs = m.objects.back().endMs > 0 ? m.objects.back().endMs
                                                : m.objects.back().timeMs;
        entries.push_back(std::move(e));
    };

    // .osz: каждый файл — отдельная карта.
    if (fs::is_directory(paths::songs(), ec)) {
        for (const fs::directory_entry &de :
             fs::directory_iterator(paths::songs(), ec)) {
            if (!de.is_regular_file(ec) ||
                lowerExt(de.path()) != ".osz") {
                continue;
            }
            beatmap::Map m;
            std::string err;
            if (beatmap::loadOsz(de.path().string(), m, err)) {
                addMap(m, de.path().string(), false);
            } else {
                std::fprintf(stderr, "map %s: %s\n",
                             de.path().filename().string().c_str(),
                             err.c_str());
            }
        }
    }

    // Распакованные карты: любой .osu внутри songs/ (включая custom/).
    // Так можно просто распаковать .osz в папку — игра её увидит.
    const std::string customPrefix = paths::customSongs();
    if (fs::is_directory(paths::songs(), ec)) {
        for (fs::recursive_directory_iterator it(paths::songs(), ec), end;
             it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            const fs::directory_entry &de = *it;
            if (!de.is_regular_file(ec) || lowerExt(de.path()) != ".osu") {
                continue;
            }
            std::string p = de.path().string();
            bool isCustom = p.rfind(customPrefix, 0) == 0;
            beatmap::Map m;
            std::string err;
            if (beatmap::loadOsu(p, m, err)) {
                addMap(m, p, isCustom);
            } else {
                std::fprintf(stderr, "map %s: %s\n",
                             de.path().filename().string().c_str(),
                             err.c_str());
            }
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                  std::string aa = a.artist();
                  std::string bb = b.artist();
                  for (char &c : aa) {
                      c = char(std::tolower(static_cast<unsigned char>(c)));
                  }
                  for (char &c : bb) {
                      c = char(std::tolower(static_cast<unsigned char>(c)));
                  }
                  if (aa != bb) {
                      return aa < bb;
                  }
                  return a.title() < b.title();
              });
}

bool DB::ensureAudio(Entry &e, std::string &err) {
    if (e.audioReady) {
        return !e.audioPath.empty();
    }
    if (e.map.audioFile.empty()) {
        err = "в карте не указан аудиофайл";
        return false;
    }

    fs::path base = e.map.oszPath.empty()
                        ? fs::path(e.map.osuPath).parent_path()
                        : fs::path(cacheDirFor(e.hash));
    std::error_code ec;
    fs::create_directories(base, ec);

    fs::path dst = base / fs::path(e.map.audioFile).filename();
    bool exists = fs::is_regular_file(dst, ec);

    if (!exists) {
        if (!e.map.oszPath.empty()) {
            if (!beatmap::extractTo(e.map.oszPath, e.map.audioFile,
                                    dst.string(), err)) {
                return false;
            }
            exists = true;
        } else {
            err = "аудиофайл не найден рядом с картой";
            return false;
        }
    }

    e.audioPath = dst.string();
    e.audioReady = exists;
    return exists;
}

bool DB::ensureCover(Entry &e, Gfx &g) {
    if (e.cover) {
        return true;
    }
    if (e.map.bgFile.empty()) {
        return false;
    }

    fs::path base = e.map.oszPath.empty()
                        ? fs::path(e.map.osuPath).parent_path()
                        : fs::path(cacheDirFor(e.hash));
    std::error_code ec;
    fs::create_directories(base, ec);
    fs::path dst = base / fs::path(e.map.bgFile).filename();

    if (!fs::is_regular_file(dst, ec)) {
        if (e.map.oszPath.empty()) {
            return false;
        }
        std::string err;
        if (!beatmap::extractTo(e.map.oszPath, e.map.bgFile, dst.string(),
                                err)) {
            return false;
        }
    }
    e.coverPath = dst.string();

    int w = 0, h = 0, comp = 0;
    unsigned char *pix =
        stbi_load(dst.string().c_str(), &w, &h, &comp, 4);
    if (!pix) {
        return false;
    }
    e.cover = g.textureFromPixels(pix, w, h);
    e.coverW = w;
    e.coverH = h;
    stbi_image_free(pix);
    return e.cover != nullptr;
}

} // namespace mapdb