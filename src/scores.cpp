#include "scores.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>

namespace scores {
namespace {

std::map<std::string, ScoreEntry> g_scores;

} // namespace

void load(const std::string &path) {
    g_scores.clear();
    std::ifstream f(path);
    if (!f) {
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream in(line);
        std::string id;
        ScoreEntry e;
        e.valid = true;
        if (!(in >> id >> e.score >> e.acc >> e.maxCombo >> e.plays >>
              e.lastPlayed >> e.counts[0] >> e.counts[1] >> e.counts[2] >>
              e.counts[3])) {
            continue;
        }
        if (!id.empty()) {
            g_scores[id] = e;
        }
    }
}

void save(const std::string &path) {
    std::ofstream f(path);
    if (!f) {
        return;
    }
    f << "# rthim best scores\n";
    f << "# id score acc combo plays last p g o m\n";
    for (const auto &kv : g_scores) {
        const ScoreEntry &e = kv.second;
        f << kv.first << ' ' << e.score << ' ' << e.acc << ' ' << e.maxCombo
          << ' ' << e.plays << ' ' << e.lastPlayed << ' ' << e.counts[0] << ' '
          << e.counts[1] << ' ' << e.counts[2] << ' ' << e.counts[3] << '\n';
    }
}

const ScoreEntry *get(const std::string &id) {
    auto it = g_scores.find(id);
    return it == g_scores.end() ? nullptr : &it->second;
}

bool submit(const std::string &id, int score, double acc, int combo,
            const int counts[4], ScoreEntry &best, ScoreEntry &before) {
    if (id.empty()) {
        best = ScoreEntry{};
        before = ScoreEntry{};
        return false;
    }

    auto it = g_scores.find(id);
    before = (it == g_scores.end()) ? ScoreEntry{} : it->second;

    bool newBest = !before.valid || score > before.score;
    if (newBest) {
        ScoreEntry e;
        e.valid = true;
        e.score = score;
        e.acc = acc;
        e.maxCombo = combo;
        e.plays = before.valid ? before.plays + 1 : 1;
        e.lastPlayed = (long long)std::time(nullptr);
        for (int i = 0; i < 4; ++i) {
            e.counts[i] = counts[i];
        }
        g_scores[id] = e;
        best = e;
    } else {
        ScoreEntry e = before;
        e.plays = before.plays + 1;
        e.lastPlayed = (long long)std::time(nullptr);
        g_scores[id] = e;
        best = e;
    }
    return newBest;
}

} // namespace scores