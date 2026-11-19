#pragma once

#include "settings.hpp"

#include <string>

// Описание строк экрана настроек: одна таблица для отрисовки и для ввода.
namespace setui {

enum class Kind { Choice, Slider, Toggle, Info };
enum class Fmt { Percent, Seconds, Ms, Multiplier, Choice, OnOff };

struct Row {
    const char *label = "";
    const char *hint = "";
    Kind kind = Kind::Info;
    Fmt fmt = Fmt::Choice;
    int *ival = nullptr;
    float *fval = nullptr;
    bool *bval = nullptr;
    float min = 0.f, max = 1.f, step = 0.05f;
    const char *const *options = nullptr;
    int nopt = 0;
    int needWorldMode = -1; // строка активна только в этом режиме мира
};

struct RowList {
    Row rows[10];
    int n = 0;
};

int catCount();
const char *catName(int i);
const char *catTag(int i);
RowList rowsFor(int cat, Settings &s);
std::string formatRow(const Row &r);
bool enabled(const Row &r, const Settings &s);
void adjust(Row &r, int dir, Settings &s);

} // namespace setui