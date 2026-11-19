#pragma once

#include "gfx.hpp"

#include <algorithm>
#include <cmath>

// Три «мира» меню. Всё, что между ними, получается интерполяцией стиля.
struct Style {
    const char *name;
    Color bgTop, bgBottom;
    Color accent, accent2;
    Color text, dim;
    Color btnFill, btnBorder, btnText, btnTextHover;
    float radiusFactor; // доля от половины высоты кнопки (1 = пилюля)
    float shear;        // скос кнопки в пикселях
    float borderWidth;
    float glow;
    float hardShadow;
    float softShadow;
    float labelSpacing; // трекинг подписи кнопки
    float titleSpacing; // трекинг заголовка
    float bob;          // амплитуда «дыхания» букв заголовка
};

inline const Style kStyles[3] = {
    // NEON — тёмный неон, параллелограммы, свечение.
    {"NEON", hex(0x05010B), hex(0x1B0838), hex(0xFF2E88), hex(0x3DF2FF),
     hex(0xF4EEFF), hex(0x9C86C8), hex(0x150B28, 0.78f), hex(0xFF2E88, 0.85f),
     hex(0xEFE6FF), hex(0x0B0616), 0.10f, 18.f, 1.5f, 1.f, 0.f, 0.f,
     4.5f, 16.f, 0.f},

    // BLOOM — пастель, мягкие пилюли, тёплые пятна.
    {"BLOOM", hex(0xFFF2EE), hex(0xFFCBDD), hex(0xEE6D9A), hex(0xB79BF0),
     hex(0x3B2138), hex(0x9B7E92), hex(0xFFFFFF, 0.92f), hex(0xFFFFFF, 0.f),
     hex(0xB84E7B), hex(0xFFFFFF), 1.f, 0.f, 0.f, 0.35f, 0.f, 9.f,
     2.4f, 10.f, 2.6f},

    // INK — бумага, брутализм, жёсткая тень, острые углы.
    {"INK", hex(0xF6F3EB), hex(0xE9E3D5), hex(0x15130F), hex(0xE2452B),
     hex(0x15130F), hex(0x6E6860), hex(0xFBFAF6), hex(0x15130F),
     hex(0x15130F), hex(0xF5F2EA), 0.f, 0.f, 3.f, 0.f, 9.f, 0.f,
     1.2f, 8.f, 0.5f},
};

inline float smoothstep01(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

// Веса для фоновых слоёв: рядом со «своим» стилем всё почти чисто,
// в середине — плавная склейка. Так меньше «серой каши» в переходе.
inline void styleWeights(float t, float w[3]) {
    t = std::clamp(t, 0.f, 2.f);
    float raw[3] = {std::max(0.f, 1.f - t),
                    std::max(0.f, 1.f - std::fabs(t - 1.f)),
                    std::max(0.f, t - 1.f)};
    float sum = 0.f;
    for (int i = 0; i < 3; ++i) {
        raw[i] = smoothstep01(raw[i]);
        sum += raw[i];
    }
    for (int i = 0; i < 3; ++i) {
        w[i] = sum > 0.0001f ? raw[i] / sum : 0.f;
    }
}

// Интерфейс (текст, кнопки, тени) переключается быстрее фона:
// держим контраст, а не усредняем его в серый.
inline float uiCurve(float f) {
    return smoothstep01((std::clamp(f, 0.f, 1.f) - 0.30f) / 0.40f);
}

inline float luminance(const Color &c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

inline Style mixStyle(const Style &a, const Style &b, float fUi, float fBg) {
    Style s{};
    s.name = (fUi < 0.5f) ? a.name : b.name;
    s.bgTop = mix(a.bgTop, b.bgTop, fBg);
    s.bgBottom = mix(a.bgBottom, b.bgBottom, fBg);
    s.accent = mix(a.accent, b.accent, fUi);
    s.accent2 = mix(a.accent2, b.accent2, fUi);
    s.text = mix(a.text, b.text, fUi);
    s.dim = mix(a.dim, b.dim, fUi);
    s.btnFill = mix(a.btnFill, b.btnFill, fUi);
    s.btnBorder = mix(a.btnBorder, b.btnBorder, fUi);
    s.btnText = mix(a.btnText, b.btnText, fUi);
    s.btnTextHover = mix(a.btnTextHover, b.btnTextHover, fUi);
    s.radiusFactor = std::lerp(a.radiusFactor, b.radiusFactor, fUi);
    s.shear = std::lerp(a.shear, b.shear, fUi);
    s.borderWidth = std::lerp(a.borderWidth, b.borderWidth, fUi);
    s.glow = std::lerp(a.glow, b.glow, fUi);
    s.hardShadow = std::lerp(a.hardShadow, b.hardShadow, fUi);
    s.softShadow = std::lerp(a.softShadow, b.softShadow, fUi);
    s.labelSpacing = std::lerp(a.labelSpacing, b.labelSpacing, fUi);
    s.titleSpacing = std::lerp(a.titleSpacing, b.titleSpacing, fUi);
    s.bob = std::lerp(a.bob, b.bob, fUi);
    return s;
}

// Посреди перехода цвета двух миров дают грязный серый. Это неизбежно,
// поэтому страхуемся: если текст сливается с фоном — гонем его в
// белый или почти чёрный.
inline void guardContrast(Style &s) {
    float bg = 0.5f * (luminance(s.bgTop) + luminance(s.bgBottom));
    if (std::fabs(luminance(s.text) - bg) < 0.33f) {
        s.text = (bg < 0.5f) ? hex(0xFFFFFF) : hex(0x141014);
    }
    // Текст лежит на плашке, а не на фоне: сравниваем именно с заливкой.
    // На стыке миров плашка серая — тогда уводим подпись в чёрный.
    float lf = luminance(s.btnFill);
    if (std::fabs(luminance(s.btnText) - lf) < 0.34f) {
        s.btnText = (lf < 0.5f) ? hex(0xFFFFFF) : hex(0x141014);
    }
    if (std::fabs(luminance(s.dim) - bg) < 0.16f) {
        s.dim = (bg < 0.5f) ? hex(0xC6B8D6) : hex(0x6E6860);
    }
}

inline Style blendStyle(float t) {
    t = std::clamp(t, 0.f, 2.f);
    Style s;
    float f = (t <= 1.f) ? t : t - 1.f;
    if (t <= 1.f) {
        s = mixStyle(kStyles[0], kStyles[1], uiCurve(f), smoothstep01(f));
    } else {
        s = mixStyle(kStyles[1], kStyles[2], uiCurve(f), smoothstep01(f));
    }

    // Между тёмным неоном и светлой пастелью середина неизбежно серая.
    // Вместо серого уводим её в «сумерки»: так контраст остаётся живым.
    float w[3];
    styleWeights(t, w);
    float dusk = 4.f * w[0] * w[1];
    if (dusk > 0.001f) {
        Color deep = hex(0x0A0410);
        s.bgTop = mix(s.bgTop, deep, 0.40f * dusk);
        s.bgBottom = mix(s.bgBottom, deep, 0.40f * dusk);
    }

    guardContrast(s);
    return s;
}