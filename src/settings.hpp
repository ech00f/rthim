#pragma once

#include <string>

// Настройки игры. Хранятся простым key=value, чтобы файл можно было
// править руками и он не ломался при обновлениях.
struct Settings {
    // --- игра -------------------------------------------------------------
    int timing = 1;          // 0 relaxed, 1 normal, 2 strict
    float approachSec = 1.35f; // сколько метеорит летит до кольца
    int offsetMs = 0;        // калибровка: + значит «ноты позже»
    float bgDim = 0.55f;     // затемнение фона карты
    bool hitSound = true;
    bool screenShake = true;
    bool particles = true;

    // --- звук -------------------------------------------------------------
    float master = 0.85f;
    float music = 0.9f;
    float sfx = 0.8f;

    // --- меню -------------------------------------------------------------
    int worldMode = 0;    // 0 — за курсором, 1 — авто, 2 — фиксированный
    int staticWorld = 0;  // 0 neon, 1 bloom, 2 ink
    float autoSpeed = 1.f;

    // --- клавиши ----------------------------------------------------------
    int keyPreset = 0;

    // --- видео ------------------------------------------------------------
    bool fullscreen = false;

    void load(const std::string &path);
    void save(const std::string &path) const;

    // Окна попадания в миллисекундах (perfect, good, ok).
    void windows(double &perfect, double &good, double &ok) const;

    // Сканы клавиш для двух ударов.
    void keys(int &k0, int &k1) const;

    // Названия пресетов и «человеческие» имена клавиш.
    static const char *presetName(int i);
    static const char *presetKeys(int i);
    static void presetLabels(int i, const char *&a, const char *&b);
};

// Общие настройки приложения (загружаются один раз при старте).
Settings &settings();