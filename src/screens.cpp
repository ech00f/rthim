#include "app.hpp"
#include "screens.hpp"
#include "settings_ui.hpp"
#include "song.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace layout {

Rect selectRow(int i, float scroll) {
    return {72.f, 148.f + float(i) * 78.f - scroll, 684.f, 70.f};
}

Rect selectPlay() { return {800.f, 626.f, 200.f, 56.f}; }
Rect selectCreate() { return {1012.f, 626.f, 200.f, 56.f}; }
Rect selectBack() { return {64.f, 626.f, 150.f, 56.f}; }
Rect selectCover() { return {800.f, 140.f, 416.f, 286.f}; }

Rect settingsCat(int i) {
    return {64.f, 148.f + float(i) * 58.f, 232.f, 50.f};
}

Rect settingsRow(int i) {
    return {320.f, 148.f + float(i) * 70.f, 896.f, 60.f};
}

Rect settingsSlider(int row) {
    Rect r = settingsRow(row);
    return {r.x + 560.f, r.y + 30.f, 300.f, 8.f};
}

int settingsCatAt(float x, float y) {
    for (int i = 0; i < setui::catCount(); ++i) {
        if (settingsCat(i).contains(x, y)) {
            return i;
        }
    }
    return -1;
}

int settingsRowAt(float x, float y) {
    for (int i = 0; i < 10; ++i) {
        Rect r = settingsRow(i);
        if (r.contains(x, y)) {
            return i;
        }
    }
    return -1;
}

Rect createRow(int i, float scroll) {
    return {72.f, 158.f + float(i) * 64.f - scroll, 544.f, 56.f};
}

Rect createStart() { return {688.f, 560.f, 240.f, 56.f}; }
Rect createBack() { return {948.f, 560.f, 240.f, 56.f}; }
Rect createNameBox() { return {688.f, 244.f, 500.f, 52.f}; }

Rect pauseButton(int i) {
    return {440.f, 320.f + float(i) * 68.f, 400.f, 54.f};
}

Rect resultsButton(int i) {
    return {392.f + float(i) * 172.f, 604.f, 156.f, 54.f};
}

} // namespace layout

namespace {

using namespace ui;

std::string fit(Gfx &g, const std::string &s, int size, float maxW,
                bool medium = false, float spacing = 0.f) {
    if (g.textWidth(s, size, spacing, medium) <= maxW) {
        return s;
    }
    std::string t = s;
    while (!t.empty()) {
        // срезаем целый UTF-8 кодпоинт
        do {
            t.pop_back();
        } while (!t.empty() &&
                 (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
        if (g.textWidth(t + "...", size, spacing, medium) <= maxW) {
            return t + "...";
        }
    }
    return "...";
}

void drawTopBar(Gfx &g, const Style &st, const App &a, const std::string &tag) {
    (void)a;
    g.text("RTHIM", 12, fade(st.text, 0.92f), 64.f, 42.f, Gfx::Left, 3.f,
           true);
    g.text("/ " + tag, 10, fade(st.dim, 0.65f), 128.f, 44.f, Gfx::Left, 2.2f,
           false);
    g.fillRect({64.f, 68.f, 1152.f, 1.f}, fade(st.dim, 0.16f));
}

void coverPlaceholder(Gfx &g, const Style &st, Rect r, const std::string &t) {
    g.fillRoundRect(r, 16.f, 0.f, fade(st.btnFill, 0.25f));
    g.ring(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 84.f, 1.f,
           fade(st.text, 0.12f));
    g.ring(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 112.f, 1.f,
           fade(st.text, 0.07f));
    std::string initials = t.empty() ? "?" : t.substr(0, 1);
    g.text(initials, 72, fade(st.text, 0.18f), r.x + r.w * 0.5f,
           r.y + r.h * 0.5f - 46.f, Gfx::Center, 0.f, true);
    g.text("NO COVER", 9, fade(st.dim, 0.5f), r.x + r.w * 0.5f,
           r.y + r.h - 26.f, Gfx::Center, 3.f, false);
}

void selectDetail(Gfx &g, const Style &st, App &a) {
    if (a.db.count() == 0) {
        return;
    }
    mapdb::Entry &e = a.db.entries[size_t(a.sel)];
    Rect cr = layout::selectCover();

    g.fillRoundRect(cr, 16.f, 0.f, fade(st.btnFill, 0.30f));
    if (e.cover) {
        float texA = float(e.coverW) / std::max(1, e.coverH);
        float boxA = cr.w / cr.h;
        Rect dst = cr;
        if (texA > boxA) {
            dst.w = cr.h * texA;
            dst.x = cr.x + (cr.w - dst.w) * 0.5f;
        } else {
            dst.h = cr.w / texA;
            dst.y = cr.y + (cr.h - dst.h) * 0.5f;
        }
        g.pushClip(cr);
        g.texture(e.cover, dst, 1.f);
        g.popClip();
        // затемнение снизу под текст
        g.gradientRect({cr.x, cr.y + cr.h * 0.55f, cr.w, cr.h * 0.45f},
                       fade(st.bgBottom, 0.f), fade(st.bgBottom, 0.85f));
    } else {
        coverPlaceholder(g, st, cr, e.title());
    }
    g.fillRoundRect(cr.inflated(1.f), 16.f, 0.f, fade(st.dim, 0.18f));

    // Заголовок и артист.
    std::string title = fit(g, e.title(), 22, cr.w, true, 0.5f);
    g.text(title, 22, fade(st.text, 0.96f), cr.x, 452.f, Gfx::Left, 0.5f,
           true);
    std::string artist = fit(g, e.artist(), 12, cr.w - 120.f);
    g.text(artist, 12, fade(st.dim, 0.85f), cr.x, 482.f, Gfx::Left, 1.2f,
           false);

    // Сложность и метаданные.
    std::string diff = fit(g, e.map.version, 10, 240.f);
    g.text(diff, 10, fade(st.accent, 0.9f), cr.x, 504.f, Gfx::Left, 1.4f,
           true);

    char bpmBuf[32], lenBuf[32], objBuf[32];
    std::snprintf(bpmBuf, sizeof bpmBuf, "%.1f", e.map.bpm);
    std::snprintf(lenBuf, sizeof lenBuf, "%s", formatTime(e.lengthMs).c_str());
    std::snprintf(objBuf, sizeof objBuf, "%d", e.objects);

    const char *labels[3] = {"BPM", "LENGTH", "NOTES"};
    const char *vals[3] = {bpmBuf, lenBuf, objBuf};
    for (int i = 0; i < 3; ++i) {
        float x = cr.x + float(i) * 108.f;
        g.text(labels[i], 8, fade(st.dim, 0.6f), x, 540.f, Gfx::Left, 2.4f,
               false);
        g.text(vals[i], 18, fade(st.text, 0.92f), x, 554.f, Gfx::Left, 0.8f,
               true);
    }

    std::string by = "MAPPED BY " + e.map.creator;
    g.text(fit(g, by, 9, cr.w), 9, fade(st.dim, 0.55f), cr.x, 594.f,
           Gfx::Left, 2.f, false);
}

void selectEmpty(Gfx &g, const Style &st) {
    g.text("NO BEATMAPS", 34, fade(st.text, 0.9f), W * 0.5f, 280.f,
           Gfx::Center, 4.f, true);
    g.text("положи .osz в папку songs/ и нажми R", 12,
           fade(st.dim, 0.8f), W * 0.5f, 330.f, Gfx::Center, 1.2f, false);
    g.text("songs/", 11, fade(st.accent, 0.9f), W * 0.5f, 356.f,
           Gfx::Center, 2.f, true);
}

void settingsValue(Gfx &g, const Style &st, const setui::Row &r, int rowIndex,
                   bool enabled) {
    Rect rr = layout::settingsRow(rowIndex);
    Color c = enabled ? st.text : fade(st.text, 0.3f);

    if (r.kind == setui::Kind::Info) {
        g.text("—", 12, fade(st.dim, 0.4f), rr.x + rr.w - 18.f, rr.y + 22.f,
               Gfx::Right, 1.f, false);
        return;
    }

    std::string val = setui::formatRow(r);
    if (r.kind == setui::Kind::Toggle) {
        Rect tr{rr.x + rr.w - 86.f, rr.y + 17.f, 52.f, 26.f};
        toggle(g, st, tr, r.bval ? *r.bval : false, enabled ? 0.6f : 0.f);
        g.text(val, 9, fade(enabled ? st.dim : fade(st.dim, 0.4f), 0.8f),
               tr.x - 14.f, tr.y + 8.f, Gfx::Right, 2.f, false);
        return;
    }
    if (r.kind == setui::Kind::Slider) {
        Rect track = layout::settingsSlider(rowIndex);
        float v01 = 0.f;
        if (r.fval) {
            v01 = (r.max > r.min)
                      ? std::clamp((*r.fval - r.min) / (r.max - r.min), 0.f,
                                   1.f)
                      : 0.f;
        }
        slider(g, st, track, v01, enabled ? 0.5f : 0.f, false);
        g.text(val, 14, fade(enabled ? st.accent : st.dim, 0.95f),
               rr.x + rr.w - 18.f, rr.y + 20.f, Gfx::Right, 1.4f, true);
        return;
    }
    if (r.kind == setui::Kind::Choice) {
        g.text(val, 14, fade(enabled ? st.accent : st.dim, 0.95f),
               rr.x + rr.w - 86.f, rr.y + 20.f, Gfx::Right, 1.4f, true);
        Color cc = fade(enabled ? st.dim : fade(st.dim, 0.4f), 0.8f);
        float y = rr.y + rr.h * 0.5f;
        float xl = rr.x + rr.w - 62.f;
        float xr = rr.x + rr.w - 28.f;
        g.line(xl + 5.f, y - 7.f, xl - 2.f, y, 2.f, cc);
        g.line(xl - 2.f, y, xl + 5.f, y + 7.f, 2.f, cc);
        g.line(xr - 5.f, y - 7.f, xr + 2.f, y, 2.f, cc);
        g.line(xr + 2.f, y, xr - 5.f, y + 7.f, 2.f, cc);
        return;
    }
    g.text(val, 14, fade(c, 0.9f), rr.x + rr.w - 18.f, rr.y + 20.f,
           Gfx::Right, 1.2f, true);
}

std::string gradeFor(double acc, bool clear) {
    if (!clear) {
        return "F";
    }
    if (acc >= 0.96) {
        return "S";
    }
    if (acc >= 0.9) {
        return "A";
    }
    if (acc >= 0.82) {
        return "B";
    }
    if (acc >= 0.72) {
        return "C";
    }
    return "D";
}

} // namespace

// --- отрисовка экранов -------------------------------------------------------

void App::draw(Gfx &g) {
    switch (screen) {
    case Screen::Menu:
        menu::draw(g, ui);
        break;
    case Screen::Select:
        drawSelect(g);
        break;
    case Screen::Settings:
        drawSettings(g);
        break;
    case Screen::Create:
        drawCreate(g);
        break;
    case Screen::Play:
        drawPlay(g);
        break;
    case Screen::Results:
        drawResults(g);
        break;
    }
}

void App::drawSelect(Gfx &g) {
    Style st = blendStyle(ui.styleT);
    ::ui::background(g, st, ui.time, ui.beat);
    drawTopBar(g, st, *this, "SELECT TRACK");

    if (db.count() == 0) {
        selectEmpty(g, st);
        ::ui::button(g, st, layout::selectBack(), "BACK", 0.f, 0.f, false);
        drawToast(g);
        menu::drawCursor(g, ui);
        return;
    }

    char cnt[64];
    std::snprintf(cnt, sizeof cnt,
                  "%d %s  ·  WHEEL TO SCROLL  ·  R TO RESCAN  ·  DROP .OSZ",
                  db.count(), db.count() == 1 ? "MAP" : "MAPS");
    g.text(cnt, 9, fade(st.dim, 0.55f), 64.f, 74.f, Gfx::Left, 2.f, false);

    // Список.
    Rect list = layout::kSelectList;
    ::ui::panel(g, st, list, 18.f, 0.30f);
    g.pushClip(list);
    for (int i = 0; i < db.count(); ++i) {
        Rect rr = layout::selectRow(i, selScroll);
        if (rr.y + rr.h < list.y - 8.f || rr.y > list.y + list.h + 8.f) {
            continue;
        }
        float hv = (i < 32) ? selHoverAnim[i] : 0.f;
        bool isSel = (i == sel);
        if (isSel) {
            g.fillRoundRect(rr, 12.f, st.shear * 0.25f,
                            fade(st.accent, 0.13f + 0.05f * hv));
        } else if (hv > 0.01f) {
            g.fillRoundRect(rr, 12.f, st.shear * 0.25f,
                            fade(st.btnFill, 0.32f * hv));
        }
        if (isSel) {
            g.fillRect({rr.x, rr.y + 12.f, 3.f, rr.h - 24.f},
                       fade(st.accent, 0.9f));
        }

        char num[16];
        std::snprintf(num, sizeof num, "%02d", i + 1);
        g.text(num, 10, fade(isSel ? st.accent : st.dim, 0.7f),
               rr.x + 18.f, rr.y + 26.f, Gfx::Left, 1.6f, true);

        std::string t = fit(g, db.entries[i].title(), 16, 360.f, true, 0.3f);
        g.text(t, 16, fade(st.text, isSel ? 0.98f : 0.8f), rr.x + 54.f,
               rr.y + 14.f, Gfx::Left, 0.4f, true);

        std::string sub = db.entries[i].artist() + "  ·  " + db.entries[i].map.version;
        sub = fit(g, sub, 10, 380.f);
        g.text(sub, 10, fade(st.dim, 0.72f), rr.x + 54.f, rr.y + 40.f,
               Gfx::Left, 1.1f, false);

        char bpm[24];
        std::snprintf(bpm, sizeof bpm, "%.0f BPM", db.entries[i].map.bpm);
        g.text(bpm, 9, fade(st.dim, 0.55f), rr.x + rr.w - 20.f,
               rr.y + 18.f, Gfx::Right, 1.4f, false);
        std::string len = formatTime(db.entries[i].lengthMs);
        g.text(len, 9, fade(st.dim, 0.55f), rr.x + rr.w - 20.f,
               rr.y + 34.f, Gfx::Right, 1.4f, false);
        char notes[24];
        std::snprintf(notes, sizeof notes, "%d NOTES", db.entries[i].objects);
        g.text(notes, 8, fade(st.dim, 0.4f), rr.x + rr.w - 20.f,
               rr.y + 50.f, Gfx::Right, 1.2f, false);
        if (db.entries[i].custom) {
            Rect chipR{rr.x + rr.w - 132.f, rr.y + 44.f, 54.f, 16.f};
            ::ui::chip(g, st, chipR, "CUSTOM", true);
        }
    }
    g.popClip();

    // Скроллбар.
    if (float(db.count()) * 78.f > list.h) {
        float frac = list.h / (float(db.count()) * 78.f);
        float thumbH = std::max(28.f, list.h * frac);
        float maxScroll =
            std::max(1.f, float(db.count()) * 78.f - list.h + 8.f);
        float y = list.y + (list.h - thumbH) * (selScroll / maxScroll);
        g.fillRoundRect({list.x + list.w + 6.f, y, 4.f, thumbH}, 2.f, 0.f,
                        fade(st.accent, 0.55f));
    }

    selectDetail(g, st, *this);

    bool has = db.count() > 0;
    ::ui::button(g, st, layout::selectPlay(), "PLAY", selBtnHover[0], 0.f,
                 true, has);
    ::ui::button(g, st, layout::selectCreate(), "CREATE", selBtnHover[1], 0.f,
                 false, has);
    ::ui::button(g, st, layout::selectBack(), "BACK", selBtnHover[2], 0.f,
                 false);

    drawToast(g);
    menu::drawCursor(g, ui);
}

void App::drawSettings(Gfx &g) {
    Style st = blendStyle(ui.styleT);
    ::ui::background(g, st, ui.time, ui.beat);
    drawTopBar(g, st, *this, "SETTINGS");
    ::ui::title(g, st, "SETTINGS", 64.f, 78.f);

    // Категории.
    for (int i = 0; i < setui::catCount(); ++i) {
        Rect r = layout::settingsCat(i);
        float hv = catHover[i];
        bool on = (i == setCat);
        if (on || hv > 0.01f) {
            g.fillRoundRect(r, 10.f, st.shear * 0.2f,
                            fade(st.accent, on ? 0.13f : 0.05f + 0.05f * hv));
        }
        if (on) {
            g.fillRect({r.x, r.y + 9.f, 3.f, r.h - 18.f}, fade(st.accent, 0.9f));
        }
        g.text(setui::catName(i), 14, fade(on ? st.text : st.dim,
                                           on ? 0.95f : 0.7f),
               r.x + 18.f, r.y + 10.f, Gfx::Left, 2.4f, true);
        g.text(setui::catTag(i), 8, fade(st.dim, on ? 0.6f : 0.35f),
               r.x + 18.f, r.y + 31.f, Gfx::Left, 1.8f, false);
    }

    g.fillRect({312.f, 148.f, 1.f, 400.f}, fade(st.dim, 0.14f));

    auto list = setui::rowsFor(setCat, cfg);
    for (int i = 0; i < list.n; ++i) {
        setui::Row &r = list.rows[i];
        Rect rr = layout::settingsRow(i);
        bool en = setui::enabled(r, cfg);
        bool on = (i == setRow);
        float hv = (i < 12) ? rowHover[i] : 0.f;
        if (on || hv > 0.01f) {
            g.fillRoundRect(rr, 12.f, st.shear * 0.2f,
                            fade(st.accent, on ? 0.09f : 0.04f * hv));
        }
        ::ui::rowLabel(g, st, r.label, r.hint, rr.x + 22.f, rr.y + 12.f, on);
        settingsValue(g, st, r, i, en);
        if (i < list.n - 1) {
            ::ui::divider(g, st, rr.x + 22.f, rr.y + rr.h - 1.f,
                        rr.w - 44.f, 0.10f);
        }
    }

    // Подсказка и назад.
    g.text("↑↓ — ROW   ←→ — CHANGE   TAB — CATEGORY   ESC — BACK", 9,
           fade(st.dim, 0.5f), 248.f, 646.f, Gfx::Left, 1.6f, false);
    ::ui::button(g, st, {64.f, 620.f, 160.f, 54.f}, "BACK", setBtnHover[0],
                 0.f, false);
    g.text("SAVED TO settings.cfg", 9, fade(st.dim, 0.4f), W - 64.f, 646.f,
           Gfx::Right, 1.6f, false);

    drawToast(g);
    menu::drawCursor(g, ui);
}

void App::drawCreate(Gfx &g) {
    Style st = blendStyle(ui.styleT);
    ::ui::background(g, st, ui.time, ui.beat);

    if (creating) {
        // --- запись -------------------------------------------------------
        drawTopBar(g, st, *this, "CREATE / RECORD");
        float cx = 640.f, cy = 356.f, R = 196.f;

        float pulse = 0.5f + 0.5f * std::sin(ui.time * 5.f);
        g.glowCircle(cx, cy, 300.f, st.accent, 0.05f + 0.04f * pulse);
        g.circle(cx, cy, R - 10.f, fade(mix(st.bgBottom, st.btnFill, 0.25f),
                                         0.6f),
                 72);
        g.ring(cx, cy, R, 2.f, fade(st.text, 0.3f));
        for (int i = 0; i < 24; ++i) {
            float a = 6.2831853f * float(i) / 24.f;
            g.line(cx + std::cos(a) * (R + 5.f), cy + std::sin(a) * (R + 5.f),
                   cx + std::cos(a) * (R + (i % 3 == 0 ? 14.f : 8.f)),
                   cy + std::sin(a) * (R + (i % 3 == 0 ? 14.f : 8.f)), 1.f,
                   fade(st.text, 0.14f));
        }
        float a0 = play.arcAngle - 0.52f;
        float a1 = play.arcAngle + 0.52f;
        g.arc(cx, cy, R + 1.f, 22.f, a0, a1, fade(st.accent, 0.22f), 48);
        g.arc(cx, cy, R + 1.f, 3.f, a0, a1, fade(st.accent, 0.95f), 48);

        // Уже поставленные ноты — точками на кольце.
        for (size_t i = 0; i < recorded.size(); ++i) {
            bool last = (i + 1 == recorded.size());
            float a = recorded[i].angle;
            float rr = R + 34.f + (last ? 6.f * recCount : 0.f);
            Color c = recorded[i].key == 0 ? st.accent : st.accent2;
            g.circle(cx + std::cos(a) * rr, cy + std::sin(a) * rr,
                     last ? 4.f + 3.f * recCount : 2.2f,
                     fade(c, last ? 0.95f : 0.6f), 16);
        }

        // Таймлайн.
        double dur = play.endMs > 1.0 ? play.endMs
                                     : std::max(1.0, song::durationMs());
        double pos = play.songMs;
        float prog = float(std::clamp(pos / dur, 0.0, 1.0));
        g.fillRect({64.f, 116.f, W - 128.f, 2.f}, fade(st.text, 0.10f));
        g.fillRect({64.f, 116.f, (W - 128.f) * prog, 2.f},
                   fade(st.accent, 0.8f));

        // REC.
        float blink = 0.55f + 0.45f * std::sin(ui.time * 6.f);
        g.circle(80.f, 74.f, 5.f, fade(hex(0xFF3355), blink), 16);
        g.text("REC", 12, fade(st.text, 0.95f), 94.f, 66.f, Gfx::Left, 2.5f,
               true);
        g.text(formatTime(pos) + " / " + formatTime(dur), 10,
               fade(st.dim, 0.75f), 64.f, 92.f, Gfx::Left, 1.4f, false);

        char cnt[64];
        std::snprintf(cnt, sizeof cnt, "%zu NOTES", recorded.size());
        g.text(cnt, 18, fade(st.text, 0.9f), W - 64.f, 64.f, Gfx::Right, 1.2f,
               true);
        g.text("Z / X — поставить · ESC — сохранить", 9,
               fade(st.dim, 0.55f), W - 64.f, 92.f, Gfx::Right, 1.6f, false);
        g.text("Наведи дугу на будущий метеорит и нажми клавишу в такт",
               10, fade(st.dim, 0.5f), 640.f, 676.f, Gfx::Center, 1.2f,
               false);

        drawToast(g);
        menu::drawCursor(g, ui);
        return;
    }

    drawTopBar(g, st, *this, "CREATE");
    ::ui::title(g, st, "CREATE", 64.f, 78.f);

    g.text("1. SONG", 9, fade(st.dim, 0.65f), 64.f, 132.f, Gfx::Left, 2.4f,
           false);
    Rect list = layout::kCreateList;
    ::ui::panel(g, st, list, 16.f, 0.30f);
    if (db.count() == 0) {
        g.text("НЕТ КАРТ — добавь .osz в songs/", 12, fade(st.dim, 0.8f),
               list.x + 20.f, list.y + 24.f, Gfx::Left, 1.2f, false);
    }
    g.pushClip(list);
    for (int i = 0; i < db.count(); ++i) {
        Rect rr = layout::createRow(i, createScroll);
        if (rr.y + rr.h < list.y - 6.f || rr.y > list.y + list.h + 6.f) {
            continue;
        }
        float hv = (i < 32) ? createHoverAnim[i] : 0.f;
        bool on = (i == createSrc);
        if (on || hv > 0.01f) {
            g.fillRoundRect(rr, 10.f, st.shear * 0.2f,
                            fade(st.accent, on ? 0.13f : 0.04f + 0.04f * hv));
        }
        std::string t = fit(g, db.entries[i].title(), 13, 380.f, true);
        g.text(t, 13, fade(st.text, on ? 0.95f : 0.75f), rr.x + 16.f,
               rr.y + 8.f, Gfx::Left, 0.4f, true);
        std::string sub = fit(g, db.entries[i].artist() + " · " +
                                       db.entries[i].map.version,
                              9, 380.f);
        g.text(sub, 9, fade(st.dim, 0.6f), rr.x + 16.f, rr.y + 30.f,
               Gfx::Left, 1.f, false);
        char bpm[24];
        std::snprintf(bpm, sizeof bpm, "%.0f", db.entries[i].map.bpm);
        g.text(bpm, 12, fade(st.dim, on ? 0.7f : 0.45f), rr.x + rr.w - 16.f,
               rr.y + 16.f, Gfx::Right, 1.f, true);
    }
    g.popClip();

    // Название.
    g.text("2. NAME", 9, fade(st.dim, 0.65f), 688.f, 196.f, Gfx::Left, 2.4f,
           false);
    Rect nb = layout::createNameBox();
    g.fillRoundRect(nb, 10.f, 0.f, fade(st.btnFill, 0.45f));
    if (nameEdit) {
        g.fillRoundRect(nb.inflated(2.f), 12.f, 0.f, fade(st.accent, 0.55f));
        g.fillRoundRect(nb, 10.f, 0.f, fade(st.btnFill, 0.6f));
    }
    std::string nm = createName.empty() ? "..." : createName;
    g.text(nm, 17, fade(st.text, 0.95f), nb.x + 16.f, nb.y + 14.f,
           Gfx::Left, 1.2f, true);
    if (nameEdit) {
        float cw = g.textWidth(nm, 17, 1.2f, true);
        float blink = 0.4f + 0.6f * std::fabs(std::sin(ui.time * 3.f));
        g.fillRect({nb.x + 18.f + cw, nb.y + 14.f, 1.5f, 22.f},
                   fade(st.accent, blink));
    }

    g.text("3. RECORD", 9, fade(st.dim, 0.65f), 688.f, 388.f, Gfx::Left, 2.4f,
           false);
    g.text("Играет выбранная песня. Каждое нажатие клавиши ставит метеорит",
           10, fade(st.dim, 0.65f), 688.f, 412.f, Gfx::Left, 1.f, false);
    g.text("в текущей позиции дуги. Можно записать всю карту за один прогон.",
           10, fade(st.dim, 0.65f), 688.f, 428.f, Gfx::Left, 1.f, false);

    if (db.count() > 0) {
        mapdb::Entry &e = db.entries[size_t(createSrc)];
        char info[160];
        std::snprintf(info, sizeof info, "SOURCE  %s — %s  (%s)",
                      e.artist().c_str(), e.title().c_str(),
                      formatTime(e.lengthMs).c_str());
        g.text(fit(g, info, 9, 500.f), 9, fade(st.accent, 0.8f), 688.f, 474.f,
               Gfx::Left, 1.2f, false);
    }

    ::ui::button(g, st, layout::createStart(), "START RECORDING", 0.f, 0.f,
               true, db.count() > 0);
    ::ui::button(g, st, layout::createBack(), "BACK", 0.f, 0.f, false);

    if (createMsgT > 0.f) {
        g.text(createMsg, 11, fade(st.accent, clamp01(createMsgT)),
               640.f, 640.f, Gfx::Center, 1.6f, true);
    }

    drawToast(g);
    menu::drawCursor(g, ui);
}

void App::drawPlay(Gfx &g) {
    Style st = blendStyle(ui.styleT);
    game::draw(g, st, play, cfg);

    if (!audioStarted) {
        float left = std::max(0.f, leadLeft);
        int n = int(std::ceil(left));
        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", n);
        float a = 0.25f + 0.55f * (left - std::floor(left));
        g.text("GET READY", 10, fade(st.dim, 0.7f), 640.f, 286.f,
               Gfx::Center, 4.f, false);
        g.text(buf, 72, fade(st.accent, a), 640.f, 320.f, Gfx::Center, 2.f,
               true);
    }

    if (paused) {
        drawPauseOverlay(g);
    }
    drawToast(g);
    menu::drawCursor(g, ui);
}

void App::drawPauseOverlay(Gfx &g) {
    Style st = blendStyle(ui.styleT);
    g.fillRect({0, 0, W, H}, fade(hex(0x000000), 0.52f));
    Rect panel{390.f, 186.f, 500.f, 380.f};
    g.fillRoundRect(panel.inflated(2.f), 22.f, 0.f, fade(st.btnBorder, 0.45f));
    g.fillRoundRect(panel, 20.f, 0.f, fade(st.bgTop, 0.985f));
    g.fillRoundRect(panel.inflated(1.f), 21.f, 0.f, fade(st.btnBorder, 0.30f));
    g.fillRoundRect(panel, 20.f, 0.f, fade(st.bgTop, 0.99f));
    g.text("PAUSED", 30, fade(st.text, 0.96f), 640.f, 222.f, Gfx::Center,
           3.f, true);
    ::ui::divider(g, st, 470.f, 268.f, 340.f, 0.18f);

    const char *labels[3] = {"RESUME", "RESTART", "QUIT TO SONGS"};
    for (int i = 0; i < 3; ++i) {
        ::ui::button(g, st, layout::pauseButton(i), labels[i], pauseHover[i],
                   0.f, i == 0);
    }
    g.text("ESC — RESUME", 9, fade(st.dim, 0.5f), 640.f, 532.f,
           Gfx::Center, 2.f, false);
}

void App::drawResults(Gfx &g) {
    Style st = blendStyle(ui.styleT);
    ::ui::background(g, st, ui.time, ui.beat);
    drawTopBar(g, st, *this, "RESULTS");

    std::string grade = gradeFor(resAcc, resClear);
    float gAlpha = 0.9f;

    // Большая оценка.
    g.glowCircle(330.f, 330.f, 170.f, resClear ? st.accent : st.dim,
                 0.10f);
    g.ring(330.f, 330.f, 108.f, 3.f,
           fade(resClear ? st.accent : st.dim, 0.75f));
    g.ring(330.f, 330.f, 124.f, 1.f,
           fade(resClear ? st.accent : st.dim, 0.25f));
    g.text(grade, 96, fade(resClear ? st.accent : st.dim, gAlpha), 330.f,
           262.f, Gfx::Center, 2.f, true);
    g.text(resClear ? "CLEAR" : "FAILED", 11,
           fade(resClear ? st.text : st.dim, 0.75f), 330.f, 392.f,
           Gfx::Center, 4.f, true);

    // Статистика.
    g.text("SCORE", 9, fade(st.dim, 0.6f), 560.f, 196.f, Gfx::Left, 2.6f,
           false);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%07d", resScore);
    g.text(buf, 34, fade(st.text, 0.96f), 560.f, 210.f, Gfx::Left, 1.f, true);

    std::snprintf(buf, sizeof buf, "%.2f%%", resAcc * 100.0);
    g.text("ACCURACY", 9, fade(st.dim, 0.6f), 560.f, 274.f, Gfx::Left, 2.6f,
           false);
    g.text(buf, 24, fade(st.text, 0.92f), 560.f, 288.f, Gfx::Left, 1.f, true);

    std::snprintf(buf, sizeof buf, "%d", resMaxCombo);
    g.text("MAX COMBO", 9, fade(st.dim, 0.6f), 760.f, 274.f, Gfx::Left, 2.6f,
           false);
    g.text(buf, 24, fade(st.text, 0.92f), 760.f, 288.f, Gfx::Left, 1.f, true);

    const char *names[4] = {"PERFECT", "GOOD", "OK", "MISS"};
    Color cols[4] = {st.accent, mix(st.accent, st.text, 0.35f), st.dim,
                     (resCounts[3] > 0) ? hex(0xE2452B) : st.dim};
    for (int i = 0; i < 4; ++i) {
        float x = 560.f + float(i % 2) * 210.f;
        float y = 350.f + float(i / 2) * 54.f;
        g.text(names[i], 9, fade(st.dim, 0.6f), x, y, Gfx::Left, 2.4f,
               false);
        std::snprintf(buf, sizeof buf, "%d", resCounts[i]);
        g.text(buf, 22, fade(cols[i], 0.95f), x + 110.f, y - 4.f, Gfx::Right,
               0.8f, true);
        g.fillRect({x, y + 30.f, 150.f, 1.f}, fade(st.dim, 0.12f));
    }

    std::string title = fit(g, resTitle, 20, 620.f, true);
    g.text(title, 20, fade(st.text, 0.9f), 560.f, 486.f, Gfx::Left, 0.4f,
           true);
    g.text(resDiff, 10, fade(st.dim, 0.65f), 560.f, 512.f, Gfx::Left, 1.4f,
           false);

    const char *labels[3] = {"RETRY", "SONGS", "MENU"};
    for (int i = 0; i < 3; ++i) {
        ::ui::button(g, st, layout::resultsButton(i), labels[i], resHover[i],
                   0.f, i == 0);
    }
    g.text("ESC — SONGS   ←→ — CHOOSE   ENTER — GO", 9,
           fade(st.dim, 0.45f), 640.f, 680.f, Gfx::Center, 1.8f, false);

    drawToast(g);
    menu::drawCursor(g, ui);
}

void App::drawToast(Gfx &g) {
    if (errorT <= 0.f || errorText.empty()) {
        return;
    }
    Style st = blendStyle(ui.styleT);
    float a = std::min(clamp01((4.f - errorT) / 0.2f), clamp01(errorT / 0.5f));
    float y = 580.f + (1.f - a) * 6.f;
    g.text(errorText, 10, fade(st.accent, 0.9f * a), 640.f, y, Gfx::Center,
           2.f, true);
}