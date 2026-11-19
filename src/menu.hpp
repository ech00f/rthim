#pragma once

#include "gfx.hpp"

#include <string>

namespace menu {

struct State {
    float cursorX = 640.f;
    float cursorY = 360.f;
    float styleT = 0.f; // 0 = NEON, 1 = BLOOM, 2 = INK
    float time = 0.f;
    int selected = 0;
    float hover[4] = {};
    float press[4] = {};
    float toast = 0.f;
    std::string toastText;
};

void update(State &s, float dt);
void draw(Gfx &g, State &s);

int hitTest(float x, float y); // -1, если курсор мимо кнопок
int buttonCount();
const char *label(int index);

} // namespace menu