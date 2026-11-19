#pragma once

#include "gfx.hpp"
#include "theme.hpp"

#include <string>

// Мелкие общие виджеты для всех экранов, кроме главного меню
// (у него своя, более парадная отрисовка).
namespace ui {

constexpr float W = 1280.f;
constexpr float H = 720.f;

inline float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

inline float approach(float cur, float target, float dt, float rate) {
    return cur + (target - cur) * (1.f - std::exp(-dt * rate));
}

// Карточка/панель под списки и настройки.
void panel(Gfx &g, const Style &st, Rect r, float radius = 14.f,
           float alpha = 0.55f);

// Кнопка. hover/press — 0..1, primary — акцентная заливка.
void button(Gfx &g, const Style &st, Rect r, const std::string &label,
            float hover, float press, bool primary = false,
            bool enabled = true);

// Маленькая подпись-чип (BPM, длина, сложность).
void chip(Gfx &g, const Style &st, Rect r, const std::string &text,
          bool accent = false);

// Тумблер.
void toggle(Gfx &g, const Style &st, Rect r, bool on, float hover);

// Ползунок 0..1.
void slider(Gfx &g, const Style &st, Rect r, float value, float hover,
            bool active);

// Ряд настройки: подпись слева, значение/контрол справа.
void rowLabel(Gfx &g, const Style &st, const std::string &text,
              const std::string &hint, float x, float y, bool selected);

// Стрелки выбора.
void arrows(Gfx &g, const Style &st, float cx, float cy, float hoverL,
            float hoverR, bool enabled);

// Заголовок экрана.
void title(Gfx &g, const Style &st, const std::string &text, float x, float y,
           float alpha = 1.f);

// Тонкая полоска-разделитель.
void divider(Gfx &g, const Style &st, float x, float y, float w,
             float alpha = 0.12f);

void background(Gfx &g, const Style &st, float t, float beat);

std::string formatTime(double ms);
std::string formatCount(int n);

} // namespace ui