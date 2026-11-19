#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>
#include <unordered_map>
#include <vector>

struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
};

inline Color rgb(float r, float g, float b, float a = 1.f) {
    return {r, g, b, a};
}

inline Color hex(Uint32 v, float a = 1.f) {
    return {float((v >> 16) & 0xFF) / 255.f, float((v >> 8) & 0xFF) / 255.f,
            float(v & 0xFF) / 255.f, a};
}

Color mix(const Color &x, const Color &y, float t);
Color withAlpha(const Color &c, float a);
Color fade(const Color &c, float mul); // умножает альфу

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    Rect inflated(float d) const { return {x - d, y - d, w + 2.f * d, h + 2.f * d}; }
};

struct Pt {
    float x = 0.f, y = 0.f;
};

// Минимальный 2D-рендерер поверх SDL3: все фигуры копятся в один вершинный
// буфер и отправляются пачкой (SDL_RenderGeometry), текст рисуется текстурами
// и сам вызывает flush() перед собой, чтобы порядок слоёв был честным.
class Gfx {
public:
    enum Align { Left, Center, Right };

    bool init(SDL_Renderer *renderer, const std::string &assetRoot);
    void shutdown();

    void begin(int outW, int outH, int winW, int winH);
    void end();

    Pt windowToLogical(float wx, float wy) const;

    void gradientRect(Rect r, Color top, Color bottom);
    void fillRect(Rect r, Color c);
    void fillRoundRect(Rect r, float radius, float shear, Color c);
    void polygon(const std::vector<Pt> &pts, Color c);
    void circle(float cx, float cy, float r, Color c, int segments = 48);
    void ellipse(float cx, float cy, float rx, float ry, float rot, Color c,
                 int segments = 28);
    void glowCircle(float cx, float cy, float r, Color c, float centerAlpha);
    void ring(float cx, float cy, float r, float thickness, Color c);
    void arc(float cx, float cy, float r, float thickness, float a0, float a1,
             Color c, int segments = 36);
    void line(float x1, float y1, float x2, float y2, float thickness,
              Color c);

    void text(const std::string &s, int size, Color c, float x, float y,
              Align align = Left, float spacing = 0.f, bool medium = false);
    float textWidth(const std::string &s, int size, float spacing = 0.f,
                    bool medium = false);
    float textHeight(int size, bool medium = false);

    void flush();

private:
    struct TextTex {
        SDL_Texture *tex = nullptr;
        float w = 0.f, h = 0.f;
    };

    Pt toScreen(float x, float y) const {
        return {ox + x * scale, oy + y * scale};
    }

    TTF_Font *font(int size, bool medium);
    const TextTex &glyphTex(Uint32 cp, int size, bool medium);

    void tri(Pt a, Pt b, Pt c, Color col);
    void triC(Pt a, Color ca, Pt b, Color cb, Pt c, Color cc);

    SDL_Renderer *ren = nullptr;
    float scale = 1.f, ox = 0.f, oy = 0.f;
    float pxPerPoint = 1.f, pyPerPoint = 1.f;
    std::string assets;
    std::unordered_map<int, TTF_Font *> fonts;
    std::unordered_map<std::string, TextTex> glyphs;
    std::vector<SDL_Vertex> verts;
};