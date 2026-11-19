#pragma once

#include "gfx.hpp"

#include <string>

namespace menu {

struct State {
    // Сырой курсор — точные попадания; сглаженный — для отрисовки.
    float rawX = 640.f, rawY = 360.f;
    float cursorX = 640.f, cursorY = 360.f;
    float cursorVX = 0.f, cursorVY = 0.f;
    float trailX = 640.f, trailY = 360.f;
    float trailVX = 0.f, trailVY = 0.f;

    // Мир меню: цель из курсора и сглаженная пружиной координата.
    float styleTarget = 0.f;
    float styleT = 0.f;
    float styleV = 0.f;

    float time = 0.f;
    float intro = 0.f;

    // Поведение мира: 0 — за курсором, 1 — авто, 2 — фиксированный.
    int worldMode = 0;
    int staticWorld = 0;
    float autoSpeed = 1.f;
    float autoT = 0.f;

    // Живые уровни из аудиодвижка (или синтетика, если звука нет).
    float beat = 0.f;         // огибающая бочки
    float bandLow = 0.f;
    float bandMid = 0.f;
    float bandHigh = 0.f;
    float beatPhase = 0.f;
    int beatIndex = 0;
    int bpm = 128;

    int selected = 0;
    int prevSelected = 0;
    float hover[4] = {};
    float press[4] = {};
    float indicatorY = 0.f;
    float indicatorV = 0.f;

    // Всплеск иконки в «медальоне» при смене пункта.
    float iconT = 10.f;

    float rippleT = 2.f; // >= 1 — эффект неактивен
    float rippleX = 0.f, rippleY = 0.f;

    float toast = 0.f;
    std::string toastText;

    // --- интерактив не-кнопочных зон -------------------------------------
    bool pointerDown = false;
    bool dragging = false;
    int dragKind = 0; // 1 — шкала миров, 2 — медальон
    float dragStartX = 0.f;
    float dragStartY = 0.f;
    float dragStartT = 0.f;

    // Временное ручное управление миром (пока курсор не уехал далеко).
    bool override = false;
    float overrideT = 0.f;
    float overrideAnchorX = 0.f;

    bool mute[3] = {};             // полосы: низ / середина / верх
    bool requestBarRestart = false;

    int hoverZone = 0;             // 0 — нет, 1 — сигнал, 2 — медальон,
                                   // 3 — шкала, 4 — доли
    int hoverBand = 0;
    float tipAlpha = 0.f;
    std::string tipText;
};

// Общая часть: время, мир, курсор, тосты. Нужна всем экранам.
void updateCore(State &s, float dt);
void update(State &s, float dt);
void draw(Gfx &g, State &s);
void drawCursor(Gfx &g, State &s);
void activate(State &s, int index);

int handleClick(State &s, float x, float y); // индекс кнопки или -1
void handleRelease(State &s);
void toast(State &s, const std::string &text);

int hitTest(float x, float y);
int buttonCount();
const char *label(int index);
Pt buttonCenter(int index);

} // namespace menu