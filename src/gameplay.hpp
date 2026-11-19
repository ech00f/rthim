#pragma once

#include "gfx.hpp"
#include "settings.hpp"
#include "theme.hpp"

#include <string>
#include <vector>

// Геймплей: кольцо в центре, активная дуга смотрит на курсор, к кольцу
// летят метеориты. Метеорит надо встретить дугой и нажать свою клавишу.
namespace game {

enum Judgment { kPerfect = 0, kGood = 1, kOk = 2, kMiss = 3 };

struct Spawn {
    double timeMs = 0;
    float angle = 0.f; // направление подлёта (радианы)
    int key = 0;       // 0 или 1
};

struct Burst {
    float angle = 0.f;
    float t = 0.f; // 0..1
    int key = 0;
};

struct State {
    std::vector<Spawn> notes;
    std::vector<unsigned char> hit;   // 0 — ждёт, 1 — сбит, 2 — промах
    std::vector<unsigned char> judge; // Judgment для сбитых
    std::vector<float> hitDelta;      // мс, для статистики
    size_t alive = 0;                 // первый неразрешённый индекс

    double songMs = 0.0;   // текущее время песни (без offset)
    double offsetMs = 0.0; // калибровка из настроек
    double startMs = 0.0;
    double endMs = 0.0;
    double approachMs = 1350.0;
    double bpm = 120.0;
    double offsetBeatMs = 0.0;

    std::string title;
    std::string diff;

    float targetAngle = -1.5708f; // куда хочет смотреть дуга (от курсора)

    float arcAngle = -1.5708f; // куда смотрит дуга
    float arcHalf = 0.52f;     // половина ширины, радианы
    float arcVel = 0.f;
    float shake = 0.f;
    float flash = 0.f;

    float keyGlow[2] = {};

    bool running = false;
    bool paused = false;
    bool finished = false;
    bool started = false;

    int score = 0;
    int combo = 0;
    int maxCombo = 0;
    int counts[4] = {}; // perfect / good / ok / miss

    int lastJudge = -1;
    float judgeT = 1.f;
    double lastDelta = 0.0;

    std::vector<Burst> bursts;

    int totalNotes() const { return int(notes.size()); }
    int resolvedNotes() const;
    double accuracy() const;
};

// startMs — абсолютное время песни, с которого начнётся игра (может быть
// меньше первой ноты: тогда будет тишина/подход).
void start(State &s, const std::vector<Spawn> &notes, double startMs,
           double bpm, double beatOffsetMs, const Settings &cfg);

void update(State &s, double songMs, float dt, const Settings &cfg);

// Направить дугу на точку (курсор). Слишком близко к центру — игнорируем.
void aimAt(State &s, float x, float y);

// Нажатие клавиши 0/1. Возвращает true, если что-то сбито.
bool press(State &s, int key, const Settings &cfg);

void draw(Gfx &g, const Style &st, State &s, const Settings &cfg);

} // namespace game