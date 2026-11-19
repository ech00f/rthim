#include "settings_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace setui {
namespace {

const char *kTiming[] = {"RELAXED", "NORMAL", "STRICT"};
const char *kWorlds[] = {"NEON", "BLOOM", "INK"};
const char *kWorldMode[] = {"CURSOR", "AUTO", "STATIC"};
const char *kKeys[] = {"Z / X", "D / F", "J / K", "A / L"};

Row choice(const char *label, const char *hint, int *v,
           const char *const *opts, int n, int needWorld = -1) {
    Row r;
    r.label = label;
    r.hint = hint;
    r.kind = Kind::Choice;
    r.fmt = Fmt::Choice;
    r.ival = v;
    r.options = opts;
    r.nopt = n;
    r.needWorldMode = needWorld;
    return r;
}

Row slider(const char *label, const char *hint, float *v, float lo, float hi,
           float step, Fmt fmt, int needWorld = -1) {
    Row r;
    r.label = label;
    r.hint = hint;
    r.kind = Kind::Slider;
    r.fmt = fmt;
    r.fval = v;
    r.min = lo;
    r.max = hi;
    r.step = step;
    r.needWorldMode = needWorld;
    return r;
}

Row toggle(const char *label, const char *hint, bool *v) {
    Row r;
    r.label = label;
    r.hint = hint;
    r.kind = Kind::Toggle;
    r.fmt = Fmt::OnOff;
    r.bval = v;
    return r;
}

Row info(const char *label, const char *hint) {
    Row r;
    r.label = label;
    r.hint = hint;
    r.kind = Kind::Info;
    return r;
}

} // namespace

int catCount() { return 6; }

const char *catName(int i) {
    switch (i) {
    case 0:
        return "GAME";
    case 1:
        return "PLAY";
    case 2:
        return "AUDIO";
    case 3:
        return "MENU";
    case 4:
        return "KEYS";
    case 5:
        return "VIDEO";
    default:
        return "";
    }
}

const char *catTag(int i) {
    switch (i) {
    case 0:
        return "TIMING · VISUALS";
    case 1:
        return "START · ASSIST";
    case 2:
        return "LEVELS";
    case 3:
        return "WORLD STYLE";
    case 4:
        return "BINDINGS";
    case 5:
        return "DISPLAY";
    default:
        return "";
    }
}

RowList rowsFor(int cat, Settings &s) {
    RowList l;
    switch (cat) {
    case 0:
        l.rows[l.n++] = choice("TIMING WINDOW", "Насколько щедро окно попадания",
                               &s.timing, kTiming, 3);
        l.rows[l.n++] = slider("APPROACH", "Время полёта метеорита",
                               &s.approachSec, 0.8f, 2.2f, 0.05f,
                               Fmt::Seconds);
        l.rows[l.n++] = slider("BACKGROUND DIM", "Затемнение фона карты",
                               &s.bgDim, 0.f, 0.9f, 0.05f, Fmt::Percent);
        l.rows[l.n++] = toggle("HIT SOUNDS", "Щелчок при попадании",
                               &s.hitSound);
        l.rows[l.n++] = toggle("SCREEN SHAKE", "Тряска на сильных ударах",
                               &s.screenShake);
        l.rows[l.n++] = toggle("PARTICLES", "Искры и вспышки", &s.particles);
        break;
    case 1:
        l.rows[l.n++] = slider("COUNTDOWN", "Отсчёт перед стартом карты",
                               &s.leadInSec, 0.5f, 6.f, 0.1f, Fmt::Seconds);
        {
            Row r;
            r.label = "OFFSET";
            r.hint = "Калибровка задержки звука";
            r.kind = Kind::Choice;
            r.fmt = Fmt::Ms;
            r.ival = &s.offsetMs;
            r.min = -150.f;
            r.max = 150.f;
            r.step = 5.f;
            l.rows[l.n++] = r;
        }
        l.rows[l.n++] = toggle("AUTOPLAY", "Игра играет сама — демо и практика",
                               &s.autoplay);
        l.rows[l.n++] = info("RESTART", "R — начать карту заново");
        break;
    case 2:
        l.rows[l.n++] = slider("MASTER", "Общая громкость", &s.master, 0.f,
                               1.f, 0.05f, Fmt::Percent);
        l.rows[l.n++] = slider("MUSIC", "Громкость песни", &s.music, 0.f, 1.f,
                               0.05f, Fmt::Percent);
        l.rows[l.n++] = slider("SFX", "Громкость ударов", &s.sfx, 0.f, 1.f,
                               0.05f, Fmt::Percent);
        break;
    case 3:
        l.rows[l.n++] = choice("WORLD CONTROL", "Кто двигает мир меню",
                               &s.worldMode, kWorldMode, 3);
        l.rows[l.n++] = choice("FIXED WORLD", "Мир для режима STATIC",
                               &s.staticWorld, kWorlds, 3, 2);
        l.rows[l.n++] = slider("AUTO SPEED", "Скорость автосмены мира",
                               &s.autoSpeed, 0.3f, 2.f, 0.1f,
                               Fmt::Multiplier, 1);
        break;
    case 4:
        l.rows[l.n++] = choice("LAYOUT", "Раскладка двух клавиш", &s.keyPreset,
                               kKeys, 4);
        l.rows[l.n++] = info("HIT KEYS", "Слева — первый цвет, справа — второй");
        l.rows[l.n++] = info("PAUSE", "ESC в игре");
        break;
    case 5:
        l.rows[l.n++] = toggle("FULLSCREEN", "Полноэкранный режим",
                               &s.fullscreen);
        l.rows[l.n++] = info("F11", "Переключить на весь экран");
        l.rows[l.n++] = info("VSYNC", "Всегда включён");
        l.rows[l.n++] = info("RESOLUTION", "Логическое полотно 1280 × 720");
        break;
    default:
        break;
    }
    return l;
}

std::string formatRow(const Row &r) {
    char buf[64];
    switch (r.kind) {
    case Kind::Choice:
        if (r.fmt == Fmt::Ms && r.ival) {
            std::snprintf(buf, sizeof buf, "%+d MS", *r.ival);
            return buf;
        }
        if (r.ival && r.options && *r.ival >= 0 && *r.ival < r.nopt) {
            return r.options[*r.ival];
        }
        return "-";
    case Kind::Slider:
        if (!r.fval) {
            return "-";
        }
        switch (r.fmt) {
        case Fmt::Percent:
            std::snprintf(buf, sizeof buf, "%d%%",
                          int(std::lround(*r.fval * 100.f)));
            break;
        case Fmt::Seconds:
            std::snprintf(buf, sizeof buf, "%.2f S", double(*r.fval));
            break;
        case Fmt::Multiplier:
            std::snprintf(buf, sizeof buf, "%.1f×", double(*r.fval));
            break;
        default:
            std::snprintf(buf, sizeof buf, "%.2f", double(*r.fval));
            break;
        }
        return buf;
    case Kind::Toggle:
        return (r.bval && *r.bval) ? "ON" : "OFF";
    default:
        return "";
    }
}

bool enabled(const Row &r, const Settings &s) {
    if (r.needWorldMode >= 0 && s.worldMode != r.needWorldMode) {
        return false;
    }
    return true;
}

void adjust(Row &r, int dir, Settings &s) {
    if (!enabled(r, s) || dir == 0) {
        return;
    }
    switch (r.kind) {
    case Kind::Choice:
        if (r.fmt == Fmt::Ms && r.ival) {
            *r.ival = std::clamp(*r.ival + dir * int(r.step), -150, 150);
        } else if (r.ival && r.nopt > 0) {
            *r.ival = (*r.ival + dir + r.nopt) % r.nopt;
        }
        break;
    case Kind::Slider:
        if (r.fval) {
            *r.fval = std::clamp(*r.fval + dir * r.step, r.min, r.max);
        }
        break;
    case Kind::Toggle:
        if (r.bval) {
            *r.bval = !*r.bval;
        }
        break;
    default:
        break;
    }
}

} // namespace setui