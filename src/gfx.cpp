#include "gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265358979f;

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

std::vector<Pt> roundRectPoly(const Rect &rect, float radius, float shear,
                              float pad) {
    Rect r = rect.inflated(pad);
    float rad = std::max(0.f, radius + pad);
    rad = std::min(rad, std::min(r.w, r.h) * 0.5f);

    constexpr int kSeg = 12;
    std::vector<Pt> pts;
    pts.reserve(4 * (kSeg + 1));

    const float cx[4] = {r.x + rad, r.x + r.w - rad, r.x + r.w - rad,
                         r.x + rad};
    const float cy[4] = {r.y + rad, r.y + rad, r.y + r.h - rad,
                         r.y + r.h - rad};
    const float a0[4] = {kPi, -kPi * 0.5f, 0.f, kPi * 0.5f};

    for (int corner = 0; corner < 4; ++corner) {
        for (int s = 0; s <= kSeg; ++s) {
            float a = a0[corner] + (kPi * 0.5f) * (float(s) / float(kSeg));
            pts.push_back({cx[corner] + std::cos(a) * rad,
                           cy[corner] + std::sin(a) * rad});
        }
    }

    if (shear != 0.f && r.h > 0.f) {
        float mid = r.y + r.h * 0.5f;
        for (Pt &p : pts) {
            p.x += shear * (mid - p.y) / r.h;
        }
    }
    return pts;
}

// Декодирование UTF-8: минимум, чтобы не падать на кириллице в будущем.
Uint32 nextCodepoint(const std::string &s, size_t &i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) -> Uint32 {
        if (i + k >= s.size()) {
            return 0;
        }
        unsigned char b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) {
            return 0;
        }
        return b & 0x3F;
    };
    if (c < 0x80) {
        i += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        Uint32 cp = (Uint32(c & 0x1F) << 6) | cont(1);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0) {
        Uint32 cp = (Uint32(c & 0x0F) << 12) | (cont(1) << 6) | cont(2);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0) {
        Uint32 cp = (Uint32(c & 0x07) << 18) | (cont(1) << 12) |
                    (cont(2) << 6) | cont(3);
        i += 4;
        return cp;
    }
    i += 1;
    return c;
}

} // namespace

Color mix(const Color &x, const Color &y, float t) {
    t = clamp01(t);
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t,
            x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

Color withAlpha(const Color &c, float a) { return {c.r, c.g, c.b, a}; }

Color fade(const Color &c, float mul) { return {c.r, c.g, c.b, c.a * mul}; }

bool Gfx::init(SDL_Renderer *renderer, const std::string &assetRoot) {
    ren = renderer;
    assets = assetRoot;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    return true;
}

void Gfx::shutdown() {
    for (auto &kv : glyphs) {
        SDL_DestroyTexture(kv.second.tex);
    }
    glyphs.clear();
    for (auto &kv : fonts) {
        TTF_CloseFont(kv.second);
    }
    fonts.clear();
}

void Gfx::begin(int outW, int outH, int winW, int winH) {
    verts.clear();

    float s = std::min(float(outW) / 1280.f, float(outH) / 720.f);
    scale = s;
    ox = (float(outW) - 1280.f * s) * 0.5f;
    oy = (float(outH) - 720.f * s) * 0.5f;

    pxPerPoint = winW > 0 ? float(outW) / float(winW) : 1.f;
    pyPerPoint = winH > 0 ? float(outH) / float(winH) : 1.f;

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    SDL_Rect clip{int(std::floor(ox)), int(std::floor(oy)),
                  int(std::ceil(1280.f * s)), int(std::ceil(720.f * s))};
    SDL_SetRenderClipRect(ren, &clip);
}

void Gfx::end() {
    flush();
    SDL_SetRenderClipRect(ren, nullptr);
}

Pt Gfx::windowToLogical(float wx, float wy) const {
    return {(wx * pxPerPoint - ox) / scale, (wy * pyPerPoint - oy) / scale};
}

void Gfx::tri(Pt a, Pt b, Pt c, Color col) { triC(a, col, b, col, c, col); }

void Gfx::triC(Pt a, Color ca, Pt b, Color cb, Pt c, Color cc) {
    SDL_Vertex va{{a.x, a.y}, {ca.r, ca.g, ca.b, ca.a}, {0.f, 0.f}};
    SDL_Vertex vb{{b.x, b.y}, {cb.r, cb.g, cb.b, cb.a}, {0.f, 0.f}};
    SDL_Vertex vc{{c.x, c.y}, {cc.r, cc.g, cc.b, cc.a}, {0.f, 0.f}};
    verts.push_back(va);
    verts.push_back(vb);
    verts.push_back(vc);
}

void Gfx::flush() {
    if (verts.empty()) {
        return;
    }
    SDL_RenderGeometry(ren, nullptr, verts.data(), int(verts.size()), nullptr,
                       0);
    verts.clear();
}

void Gfx::gradientRect(Rect r, Color top, Color bottom) {
    Pt tl = toScreen(r.x, r.y);
    Pt tr = toScreen(r.x + r.w, r.y);
    Pt bl = toScreen(r.x, r.y + r.h);
    Pt br = toScreen(r.x + r.w, r.y + r.h);
    triC(tl, top, tr, top, br, bottom);
    triC(tl, top, br, bottom, bl, bottom);
}

void Gfx::fillRect(Rect r, Color c) { gradientRect(r, c, c); }

void Gfx::fillRoundRect(Rect r, float radius, float shear, Color c) {
    if (c.a <= 0.002f || r.w <= 0.f || r.h <= 0.f) {
        return;
    }

    std::vector<Pt> inner = roundRectPoly(r, radius, shear, 0.f);
    std::vector<Pt> outer = roundRectPoly(r, radius, shear, 1.2f);
    const size_t n = inner.size();

    Color clear = withAlpha(c, 0.f);
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        Pt a{toScreen(inner[i].x, inner[i].y)};
        Pt b{toScreen(inner[j].x, inner[j].y)};
        Pt oa{toScreen(outer[i].x, outer[i].y)};
        Pt ob{toScreen(outer[j].x, outer[j].y)};
        triC(a, c, b, c, ob, clear);
        triC(a, c, ob, clear, oa, clear);
    }

    float cx = 0.f, cy = 0.f;
    for (const Pt &p : inner) {
        cx += p.x;
        cy += p.y;
    }
    cx /= float(n);
    cy /= float(n);
    Pt center = toScreen(cx, cy);
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        tri(center, toScreen(inner[i].x, inner[i].y),
            toScreen(inner[j].x, inner[j].y), c);
    }
}

void Gfx::circle(float cx, float cy, float r, Color c, int segments) {
    if (c.a <= 0.002f || r <= 0.f) {
        return;
    }
    Pt center = toScreen(cx, cy);
    Pt prev;
    for (int i = 0; i <= segments; ++i) {
        float a = kPi * 2.f * float(i) / float(segments);
        Pt p = toScreen(cx + std::cos(a) * r, cy + std::sin(a) * r);
        if (i > 0) {
            tri(center, prev, p, c);
        }
        prev = p;
    }
}

void Gfx::glowCircle(float cx, float cy, float r, Color c, float centerAlpha) {
    if (centerAlpha <= 0.002f || r <= 0.f) {
        return;
    }
    constexpr int kSeg = 48;
    Color inner = withAlpha(c, centerAlpha);
    Color mid = withAlpha(c, centerAlpha * 0.45f);
    Color edge = withAlpha(c, 0.f);

    Pt center = toScreen(cx, cy);
    for (int i = 0; i < kSeg; ++i) {
        float a0 = kPi * 2.f * float(i) / float(kSeg);
        float a1 = kPi * 2.f * float(i + 1) / float(kSeg);
        Pt m0 = toScreen(cx + std::cos(a0) * r * 0.55f,
                         cy + std::sin(a0) * r * 0.55f);
        Pt m1 = toScreen(cx + std::cos(a1) * r * 0.55f,
                         cy + std::sin(a1) * r * 0.55f);
        Pt e0 = toScreen(cx + std::cos(a0) * r, cy + std::sin(a0) * r);
        Pt e1 = toScreen(cx + std::cos(a1) * r, cy + std::sin(a1) * r);

        triC(center, inner, m0, mid, m1, mid);
        triC(m0, mid, e0, edge, e1, edge);
        triC(m0, mid, e1, edge, m1, mid);
    }
}

void Gfx::ring(float cx, float cy, float r, float thickness, Color c) {
    if (c.a <= 0.002f || r <= thickness) {
        return;
    }
    constexpr int kSeg = 64;
    float ri = r - thickness;
    for (int i = 0; i < kSeg; ++i) {
        float a0 = kPi * 2.f * float(i) / float(kSeg);
        float a1 = kPi * 2.f * float(i + 1) / float(kSeg);
        Pt o0 = toScreen(cx + std::cos(a0) * r, cy + std::sin(a0) * r);
        Pt o1 = toScreen(cx + std::cos(a1) * r, cy + std::sin(a1) * r);
        Pt i0 = toScreen(cx + std::cos(a0) * ri, cy + std::sin(a0) * ri);
        Pt i1 = toScreen(cx + std::cos(a1) * ri, cy + std::sin(a1) * ri);
        tri(o0, o1, i1, c);
        tri(o0, i1, i0, c);
    }
}

void Gfx::line(float x1, float y1, float x2, float y2, float thickness,
               Color c) {
    if (c.a <= 0.002f) {
        return;
    }
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f) {
        return;
    }
    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    Pt a = toScreen(x1 + nx, y1 + ny);
    Pt b = toScreen(x2 + nx, y2 + ny);
    Pt c2 = toScreen(x2 - nx, y2 - ny);
    Pt d = toScreen(x1 - nx, y1 - ny);
    tri(a, b, c2, c);
    tri(a, c2, d, c);
}

TTF_Font *Gfx::font(int size, bool medium) {
    int key = size * 2 + (medium ? 1 : 0);
    auto it = fonts.find(key);
    if (it != fonts.end()) {
        return it->second;
    }
    std::string path = assets + (medium ? "/fonts/Roboto-Medium.ttf"
                                       : "/fonts/Roboto-Regular.ttf");
    TTF_Font *f = TTF_OpenFont(path.c_str(), float(size));
    if (!f) {
        std::fprintf(stderr, "font load failed (%s): %s\n", path.c_str(),
                     SDL_GetError());
        return nullptr;
    }
    TTF_SetFontKerning(f, false);
    fonts[key] = f;
    return f;
}

const Gfx::TextTex &Gfx::glyphTex(Uint32 cp, int size, Color c, bool medium) {
    char key[64];
    std::snprintf(key, sizeof key, "%X|%d|%c|%02X%02X%02X%02X", cp, size,
                  medium ? 'm' : 'r', int(clamp01(c.r) * 255.f),
                  int(clamp01(c.g) * 255.f), int(clamp01(c.b) * 255.f),
                  int(clamp01(c.a) * 255.f));
    std::string k(key);
    auto it = glyphs.find(k);
    if (it != glyphs.end()) {
        return it->second;
    }

    static TextTex empty{};
    TTF_Font *f = font(size, medium);
    if (!f) {
        return empty;
    }

    SDL_Color sc{Uint8(clamp01(c.r) * 255.f), Uint8(clamp01(c.g) * 255.f),
                 Uint8(clamp01(c.b) * 255.f), Uint8(clamp01(c.a) * 255.f)};
    SDL_Surface *surf = TTF_RenderGlyph_Blended(f, cp, sc);
    if (!surf) {
        return empty;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    TextTex tt;
    tt.tex = tex;
    tt.w = float(surf->w);
    tt.h = float(surf->h);
    SDL_DestroySurface(surf);
    if (!tt.tex) {
        return empty;
    }
    SDL_SetTextureScaleMode(tt.tex, SDL_SCALEMODE_LINEAR);
    auto res = glyphs.emplace(std::move(k), tt);
    return res.first->second;
}

float Gfx::textWidth(const std::string &s, int size, float spacing,
                     bool medium) {
    TTF_Font *f = font(size, medium);
    if (!f || s.empty()) {
        return 0.f;
    }
    float w = 0.f;
    size_t i = 0;
    int chars = 0;
    while (i < s.size()) {
        Uint32 cp = nextCodepoint(s, i);
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        TTF_GetGlyphMetrics(f, cp, &minx, &maxx, &miny, &maxy, &adv);
        w += float(adv);
        ++chars;
    }
    if (chars > 1) {
        w += spacing * float(chars - 1);
    }
    return w;
}

float Gfx::textHeight(int size, bool medium) {
    TTF_Font *f = font(size, medium);
    if (!f) {
        return 0.f;
    }
    return float(TTF_GetFontHeight(f));
}

void Gfx::text(const std::string &s, int size, Color c, float x, float y,
               Align align, float spacing, bool medium) {
    if (s.empty() || c.a <= 0.002f) {
        return;
    }
    flush();

    TTF_Font *f = font(size, medium);
    if (!f) {
        return;
    }
    float total = textWidth(s, size, spacing, medium);
    float pen = x;
    if (align == Center) {
        pen -= total * 0.5f;
    } else if (align == Right) {
        pen -= total;
    }
    int ascent = TTF_GetFontAscent(f);
    (void)ascent;

    size_t i = 0;
    while (i < s.size()) {
        Uint32 cp = nextCodepoint(s, i);
        int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
        TTF_GetGlyphMetrics(f, cp, &minx, &maxx, &miny, &maxy, &adv);
        if (cp != ' ' && cp != '\t') {
            const TextTex &tt = glyphTex(cp, size, c, medium);
            if (tt.tex) {
                // SDL_ttf 3 отдаёт глиф уже внутри полной строки: он
                // выровнен по baseline и включает боковые отступы.
                SDL_FRect dst{ox + pen * scale, oy + y * scale,
                              tt.w * scale, tt.h * scale};
                SDL_RenderTexture(ren, tt.tex, nullptr, &dst);
            }
        }
        pen += float(adv) + spacing;
    }
}