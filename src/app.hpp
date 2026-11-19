#pragma once

#include "gameplay.hpp"
#include "mapdb.hpp"
#include "menu.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

struct App {
    enum class Screen { Menu, Select, Settings, Create, Play, Results };
    Screen screen = Screen::Menu;

    menu::State ui;
    mapdb::DB db;
    Settings cfg;
    Gfx *gfx = nullptr;

    float mouseX = 640.f, mouseY = 360.f;
    bool mouseDown = false;
    bool quit = false;
    std::string errorText;
    float errorT = 0.f;

    // --- выбор карты ------------------------------------------------------
    int sel = 0;
    float selScroll = 0.f;
    float selScrollV = 0.f;
    int selHover = -1;
    float selHoverAnim[32] = {};
    float selBtnHover[3] = {};

    // --- настройки --------------------------------------------------------
    int setCat = 0;
    int setRow = 0;
    int setHoverCat = -1;
    int setHoverRow = -1;
    int setDrag = -1;
    float catHover[8] = {};
    float rowHover[12] = {};
    float setBtnHover[1] = {};

    // --- создание ---------------------------------------------------------
    int createSrc = 0;
    float createScroll = 0.f;
    int createHover = -1;
    float createHoverAnim[32] = {};
    float createBtnHover[2] = {};
    std::string createName = "MY CHART";
    bool creating = false;
    bool nameEdit = true;
    int pendingRec = -1;
    double lastRecMs = -1000.0;
    std::vector<game::Spawn> recorded;
    float recCount = 0.f; // для анимации вспышки
    std::string createMsg;
    float createMsgT = 0.f;

    // --- игра -------------------------------------------------------------
    game::State play;
    int playMap = -1;
    std::string loadedPath;
    int frozenWorld = 0;
    bool paused = false;
    bool audioStarted = false;
    double audioStartMs = 0.0;
    float leadLeft = 0.f;
    int pauseSel = 0;
    float pauseHover[4] = {};

    // --- результаты -------------------------------------------------------
    bool resClear = false;
    double resAcc = 0.0;
    int resScore = 0;
    int resMaxCombo = 0;
    int resCounts[4] = {};
    int resSel = 0;
    float resHover[4] = {};
    std::string resTitle, resDiff;

    bool wantFullscreenApply = false;

    bool init(Gfx &g);
    void update(float dt);
    void draw(Gfx &g);
    void handleEvent(const SDL_Event &e);
    bool menuMusicActive() const;
    void mouseMoved(float x, float y);

    void go(Screen s);
    void openSelect();
    void openSettings(int cat);
    void openCreate();
    void startMap(int index);
    void retryMap();
    void finishMap(bool clear);
    void saveCustom();
    void loadCoverFor(int index);

    void updateMenu(float dt);
    void updateSelect(float dt);
    void updateSettings(float dt);
    void updateCreate(float dt);
    void updatePlay(float dt);
    void updateResults(float dt);

    void clickMenu(float x, float y);
    void clickSelect(float x, float y);
    void clickSettings(float x, float y);
    void clickCreate(float x, float y);
    void keySelect(int sc, bool repeat);
    void keySettings(int sc, bool repeat);
    void keyCreate(int sc, bool repeat);
    void keyPause(int sc);
    void keyResults(int sc);
    void keyPlay(int sc);
    void adjustSetting(int row, int dir);
    void setSlider(int row, float x);

    // отрисовка (screens.cpp)
    void drawSelect(Gfx &g);
    void drawSettings(Gfx &g);
    void drawCreate(Gfx &g);
    void drawPlay(Gfx &g);
    void drawResults(Gfx &g);
    void drawPauseOverlay(Gfx &g);
    void drawToast(Gfx &g);
};