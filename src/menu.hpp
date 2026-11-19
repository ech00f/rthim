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

    int selected = 0;
    float hover[4] = {};
    float press[4] = {};
    float indicatorY = 0.f;
    float indicatorV = 0.f;

    float rippleT = 2.f; // >= 1 — эффект неактивен
    float rippleX = 0.f, rippleY = 0.f;

    float toast = 0.f;
    std::string toastText;
};

void update(State &s, float dt);
void draw(Gfx &g, State &s);
void activate(State &s, int index);

int hitTest(float x, float y);
int buttonCount();
const char *label(int index);
Pt buttonCenter(int index);

} // namespace menu