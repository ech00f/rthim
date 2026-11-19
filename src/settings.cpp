#include "settings.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

Settings g_settings;

int readInt(const std::string &v, int def) {
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

float readFloat(const std::string &v, float def) {
    try {
        return std::stof(v);
    } catch (...) {
        return def;
    }
}

int readBool(const std::string &v, bool def) {
    if (v == "1" || v == "true" || v == "on") {
        return 1;
    }
    if (v == "0" || v == "false" || v == "off") {
        return 0;
    }
    return def ? 1 : 0;
}

} // namespace

Settings &settings() { return g_settings; }

void Settings::load(const std::string &path) {
    std::ifstream f(path);
    if (!f) {
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\r')) {
            k.pop_back();
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) {
            v.pop_back();
        }

        if (k == "timing") {
            timing = std::clamp(readInt(v, timing), 0, 2);
        } else if (k == "approach") {
            approachSec = std::clamp(readFloat(v, approachSec), 0.7f, 2.4f);
        } else if (k == "offset") {
            offsetMs = std::clamp(readInt(v, offsetMs), -250, 250);
        } else if (k == "bgdim") {
            bgDim = std::clamp(readFloat(v, bgDim), 0.f, 0.95f);
        } else if (k == "hitsound") {
            hitSound = readBool(v, hitSound) != 0;
        } else if (k == "shake") {
            screenShake = readBool(v, screenShake) != 0;
        } else if (k == "particles") {
            particles = readBool(v, particles) != 0;
        } else if (k == "master") {
            master = std::clamp(readFloat(v, master), 0.f, 1.f);
        } else if (k == "music") {
            music = std::clamp(readFloat(v, music), 0.f, 1.f);
        } else if (k == "sfx") {
            sfx = std::clamp(readFloat(v, sfx), 0.f, 1.f);
        } else if (k == "worldmode") {
            worldMode = std::clamp(readInt(v, worldMode), 0, 2);
        } else if (k == "staticworld") {
            staticWorld = std::clamp(readInt(v, staticWorld), 0, 2);
        } else if (k == "autospeed") {
            autoSpeed = std::clamp(readFloat(v, autoSpeed), 0.25f, 2.f);
        } else if (k == "keypreset") {
            keyPreset = std::clamp(readInt(v, keyPreset), 0, 3);
        } else if (k == "fullscreen") {
            fullscreen = readBool(v, fullscreen) != 0;
        }
    }
}

void Settings::save(const std::string &path) const {
    std::ofstream f(path);
    if (!f) {
        return;
    }
    f << "# rthim settings\n";
    f << "timing=" << timing << "\n";
    f << "approach=" << approachSec << "\n";
    f << "offset=" << offsetMs << "\n";
    f << "bgdim=" << bgDim << "\n";
    f << "hitsound=" << (hitSound ? 1 : 0) << "\n";
    f << "shake=" << (screenShake ? 1 : 0) << "\n";
    f << "particles=" << (particles ? 1 : 0) << "\n";
    f << "master=" << master << "\n";
    f << "music=" << music << "\n";
    f << "sfx=" << sfx << "\n";
    f << "worldmode=" << worldMode << "\n";
    f << "staticworld=" << staticWorld << "\n";
    f << "autospeed=" << autoSpeed << "\n";
    f << "keypreset=" << keyPreset << "\n";
    f << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
}

void Settings::windows(double &perfect, double &good, double &ok) const {
    switch (timing) {
    case 0:
        perfect = 70.0;
        good = 130.0;
        ok = 210.0;
        break;
    case 2:
        perfect = 30.0;
        good = 65.0;
        ok = 115.0;
        break;
    default:
        perfect = 45.0;
        good = 95.0;
        ok = 160.0;
        break;
    }
}

void Settings::keys(int &k0, int &k1) const {
    switch (keyPreset) {
    case 1:
        k0 = SDL_SCANCODE_D;
        k1 = SDL_SCANCODE_F;
        break;
    case 2:
        k0 = SDL_SCANCODE_J;
        k1 = SDL_SCANCODE_K;
        break;
    case 3:
        k0 = SDL_SCANCODE_A;
        k1 = SDL_SCANCODE_L;
        break;
    default:
        k0 = SDL_SCANCODE_Z;
        k1 = SDL_SCANCODE_X;
        break;
    }
}

const char *Settings::presetName(int i) {
    switch (i) {
    case 1:
        return "D / F";
    case 2:
        return "J / K";
    case 3:
        return "A / L";
    default:
        return "Z / X";
    }
}

void Settings::presetLabels(int i, const char *&a, const char *&b) {
    switch (i) {
    case 1:
        a = "D";
        b = "F";
        break;
    case 2:
        a = "J";
        b = "K";
        break;
    case 3:
        a = "A";
        b = "L";
        break;
    default:
        a = "Z";
        b = "X";
        break;
    }
}

const char *Settings::presetKeys(int i) {
    switch (i) {
    case 1:
        return "HOME ROW";
    case 2:
        return "RIGHT HAND";
    case 3:
        return "OUTER KEYS";
    default:
        return "CLASSIC";
    }
}