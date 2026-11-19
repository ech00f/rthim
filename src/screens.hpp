#pragma once

#include "gfx.hpp"

// Геометрия экранов: одна и та же для отрисовки и попаданий мышью.
namespace layout {

// --- выбор карты ---------------------------------------------------------
constexpr Rect kSelectList{64.f, 140.f, 700.f, 452.f};
Rect selectRow(int i, float scroll);
Rect selectPlay();
Rect selectCreate();
Rect selectBack();
Rect selectCover();

// --- настройки -----------------------------------------------------------
constexpr Rect kSetSide{64.f, 140.f, 232.f, 320.f};
Rect settingsCat(int i);
Rect settingsRow(int i);
Rect settingsSlider(int row);
int settingsCatAt(float x, float y);
int settingsRowAt(float x, float y);

// --- создание карты ------------------------------------------------------
constexpr Rect kCreateList{64.f, 150.f, 560.f, 380.f};
Rect createRow(int i, float scroll);
Rect createStart();
Rect createBack();
Rect createNameBox();

// --- пауза и результаты --------------------------------------------------
Rect pauseButton(int i);
Rect resultsButton(int i);

} // namespace layout