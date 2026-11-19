#include "app.hpp"
#include "beatmap.hpp"
#include "audio.hpp"
#include "gfx.hpp"
#include "menu.hpp"
#include "paths.hpp"
#include "song.hpp"
#include "theme.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::string findAssets() {
    std::vector<std::string> roots;
    if (const char *base = SDL_GetBasePath()) {
        roots.push_back(std::string(base) + "assets");
        roots.push_back(std::string(base) + "../assets");
    }
    roots.push_back("assets");
    roots.push_back("../assets");

    for (const std::string &r : roots) {
        std::error_code ec;
        if (std::filesystem::exists(r + "/fonts/Roboto-Regular.ttf", ec)) {
            return r;
        }
    }
    return "assets";
}

// Если аудиоустройства нет — интерфейс всё равно должен биться.
void syntheticBeat(menu::State &s) {
    int active = std::clamp(int(std::round(s.styleT)), 0, 2);
    float bpm = float(audio::styleBpm(active));
    float beats = s.time * bpm / 60.f;
    float phase = beats - std::floor(beats);
    s.bpm = int(bpm);
    s.beat = std::pow(std::max(0.f, std::sin(phase * 6.2831853f)), 8.f);
    s.beatIndex = int(std::floor(beats)) & 3;
    s.beatPhase = phase;
    s.bandLow = 0.35f + 0.40f * (0.5f + 0.5f * std::sin(s.time * 2.1f));
    s.bandMid = 0.30f + 0.38f * (0.5f + 0.5f * std::sin(s.time * 3.3f + 1.f));
    s.bandHigh = 0.18f + 0.32f * (0.5f + 0.5f * std::sin(s.time * 5.7f + 2.f));
}

void saveShot(SDL_Renderer *ren, const char *path) {
    SDL_Surface *surf = SDL_RenderReadPixels(ren, nullptr);
    if (!surf) {
        std::fprintf(stderr, "read pixels failed: %s\n", SDL_GetError());
        return;
    }
    if (SDL_SavePNG(surf, path)) {
        std::printf("saved %s\n", path);
    } else {
        std::fprintf(stderr, "save png failed: %s\n", SDL_GetError());
    }
    SDL_DestroySurface(surf);
}

} // namespace

int main(int argc, char **argv) {
    bool shotMode = false;
    bool bounceMode = false;
    bool shotsMode = false;
    bool selftestMode = false;
    bool autoplayMode = false;
    bool listMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--shot") {
            shotMode = true;
        } else if (std::string(argv[i]) == "--bounce") {
            bounceMode = true;
        } else if (std::string(argv[i]) == "--shots") {
            shotsMode = true;
        } else if (std::string(argv[i]) == "--selftest") {
            selftestMode = true;
        } else if (std::string(argv[i]) == "--autoplay") {
            autoplayMode = true;
        } else if (std::string(argv[i]) == "--list") {
            listMode = true;
        }
    }

    if (bounceMode) {
        // Офлайн-рендер трёх битов в WAV — можно послушать без запуска окна.
        return audio::bounce("/tmp/opencode", 8.f) ? 0 : 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (!TTF_Init()) {
        std::fprintf(stderr, "TTF_Init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "rthim", 1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) {
        std::fprintf(stderr, "window failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(win, 640, 360);

    SDL_Renderer *ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) {
        std::fprintf(stderr, "renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_HideCursor();
    SDL_StartTextInput(win);

    Gfx g;
    g.init(ren, findAssets());
    paths::init(findAssets());

    if (!audio::init()) {
        std::fprintf(stderr, "audio: no output device, using synthetic pulse\n");
    }

    auto cleanup = [&]() {
        song::stop();
        song::unload();
        audio::shutdown();
        g.shutdown();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
    };

    if (shotMode) {
        menu::State s;
        s.intro = 4.f;
        s.time = 2.f;
        const struct {
            float x, y;
        } shots[] = {{150.f, 337.f}, {410.f, 415.f}, {640.f, 337.f},
                     {870.f, 493.f}, {1130.f, 337.f}};
        int idx = 0;
        for (const auto &sh : shots) {
            s.rawX = sh.x;
            s.rawY = sh.y;
            for (int k = 0; k < 90; ++k) {
                menu::update(s, 1.f / 60.f);
            }
            s.beat = 0.9f;
            s.bandLow = 0.85f;
            s.bandMid = 0.60f;
            s.bandHigh = 0.34f;
            s.beatIndex = 1;
            s.beatPhase = 0.2f;
            s.bpm = audio::styleBpm(
                std::clamp(int(std::round(s.styleT)), 0, 2));

            int pw = 0, ph = 0, ww = 0, wh = 0;
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            SDL_GetWindowSize(win, &ww, &wh);
            g.begin(pw, ph, ww, wh);
            menu::draw(g, s);
            g.end();

            char path[128];
            std::snprintf(path, sizeof path, "/tmp/opencode/menu_%d.png",
                          ++idx);
            saveShot(ren, path);
            SDL_RenderPresent(ren);
            SDL_Delay(80);
        }
        cleanup();
        return 0;
    }

    App app;
    if (!app.init(g)) {
        std::fprintf(stderr, "app init failed\n");
        cleanup();
        return 1;
    }
    if (app.cfg.fullscreen) {
        SDL_SetWindowFullscreen(win, true);
    }

    if (listMode) {
        app.db.scan();
        app.resortMaps();
        for (int sm = 0; sm < 5; ++sm) {
            app.sortMode = sm;
            app.resortMaps();
            if (app.db.count() > 0) {
                std::printf("sort %-6s -> %s / %s\n", app.sortName(),
                            app.db.entries[0].title().c_str(),
                            app.db.entries.back().title().c_str());
            }
        }
        app.sortMode = 0;
        app.resortMaps();
        std::printf("maps: %d (sort %s)\n", app.db.count(), app.sortName());
        for (int i = 0; i < app.db.count(); ++i) {
            const mapdb::Entry &e = app.db.entries[size_t(i)];
            const ScoreEntry *best = scores::get(e.id);
            std::printf("%2d  %-28s  %-18s bpm=%.1f len=%.1fs custom=%d "
                        "best=%d\n",
                        i, e.title().c_str(), e.map.version.c_str(),
                        e.map.bpm, e.lengthMs / 1000.0, int(e.custom),
                        best ? best->score : 0);
        }
        cleanup();
        return 0;
    }

    if (autoplayMode) {
        if (app.db.count() == 0) {
            std::fprintf(stderr, "autoplay: no maps\n");
            cleanup();
            return 1;
        }
        app.cfg.autoplay = true;
        app.startMap(0);
        if (app.screen != App::Screen::Play) {
            std::fprintf(stderr, "autoplay: start failed: %s\n",
                         app.errorText.c_str());
            cleanup();
            return 1;
        }
        Uint64 prevA = SDL_GetTicks();
        Uint64 t0 = prevA;
        while (SDL_GetTicks() - t0 < 12000 && app.screen == App::Screen::Play) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) {
                    app.quit = true;
                }
            }
            Uint64 nowA = SDL_GetTicks();
            float dt = std::min(0.05f, float(nowA - prevA) / 1000.f);
            prevA = nowA;
            app.update(dt);

            SDL_Delay(2);
        }
        // Проверяем, что ESC в паузе действительно снимает паузу.
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_SCANCODE_ESCAPE;
        app.handleEvent(ev);
        bool pausedOnce = app.paused;
        app.handleEvent(ev);
        bool pausedTwice = app.paused;

        int scoreBeforeFinish = app.play.score;
        double accBeforeFinish = app.play.accuracy();
        double posBeforeFinish = song::positionMs();
        app.finishMap(app.play.counts[game::kMiss] * 3 <
                          app.play.totalNotes() &&
                      accBeforeFinish > 0.55);

        std::printf("autoplay: audioStarted=%d pos=%.1f notes=%d "
                    "counts=%d/%d/%d/%d score=%d acc=%.3f pause=%d/%d "
                    "best=%s/%d\n",
                    int(app.audioStarted), posBeforeFinish,
                    app.play.totalNotes(), app.play.counts[0],
                    app.play.counts[1], app.play.counts[2],
                    app.play.counts[3], scoreBeforeFinish,
                    accBeforeFinish, int(pausedOnce), int(pausedTwice),
                    app.resNewBest ? "NEW" : "OLD", app.resBest.score);
        song::stop();
        cleanup();
        if (!pausedOnce || pausedTwice) {
            std::fprintf(stderr, "autoplay: pause toggle failed\n");
            return 3;
        }
        return (app.play.counts[game::kMiss] == 0) ? 0 : 2;
    }

    if (selftestMode) {
        if (app.db.count() == 0) {
            std::fprintf(stderr, "selftest: no maps\n");
            cleanup();
            return 1;
        }
        app.createSrc = 0;
        app.createName = "SELFTEST";
        app.creating = true;
        app.recorded.clear();
        const float ang[6] = {0.f, 1.f, 2.f, 3.f, -1.f, -2.f};
        for (int i = 0; i < 6; ++i) {
            game::Spawn n;
            n.timeMs = 1000.0 + i * 700.0;
            n.angle = ang[i];
            n.key = i % 2;
            app.recorded.push_back(n);
        }
        app.saveCustom();

        int found = -1;
        for (int i = 0; i < app.db.count(); ++i) {
            if (app.db.entries[i].custom &&
                app.db.entries[i].map.version == "SELFTEST") {
                found = i;
            }
        }
        if (found < 0) {
            std::fprintf(stderr, "selftest: saved map not found\n");
            cleanup();
            return 1;
        }
        mapdb::Entry &e = app.db.entries[found];
        beatmap::Map check;
        std::string err;
        bool ok = beatmap::loadOsu(e.sourcePath, check, err);
        std::printf("selftest map: %s objects=%zu bpm=%.2f\n",
                    e.sourcePath.c_str(), check.objects.size(), check.bpm);
        if (!ok || check.objects.size() != 6) {
            std::fprintf(stderr, "selftest: reload failed: %s\n", err.c_str());
            cleanup();
            return 1;
        }
        app.startMap(found);
        if (app.screen != App::Screen::Play) {
            std::fprintf(stderr, "selftest: play failed: %s\n",
                         app.errorText.c_str());
            cleanup();
            return 1;
        }
        std::printf("selftest play ok: notes=%d title=%s\n",
                    app.play.totalNotes(), app.play.title.c_str());

        // Проверяем логику лучшего результата, не трогая scores.cfg.
        int testCounts[4] = {6, 0, 0, 0};
        ScoreEntry best, before;
        bool newBest = scores::submit("selftest-entry", 123456, 1.0, 6,
                                      testCounts, best, before);
        const ScoreEntry *saved = scores::get("selftest-entry");
        std::printf("selftest score: %d (expected 123456), new=%d\n",
                    saved ? saved->score : 0, int(newBest));
        if (!saved || saved->score != 123456 || !newBest) {
            std::fprintf(stderr, "selftest: score submit failed\n");
            cleanup();
            return 1;
        }

        app.cfg.mapSnap = true;
        app.play.bpm = 120.0;
        app.play.offsetBeatMs = 0.0;
        std::printf("selftest snap: 1010 -> %.0f (expected 1000)\n",
                    app.snapTime(1010.0));

        song::stop();
        cleanup();
        return 0;
    }

    if (shotsMode) {
        auto snap = [&](const char *name) {
            int pw = 0, ph = 0, ww = 0, wh = 0;
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            SDL_GetWindowSize(win, &ww, &wh);
            g.begin(pw, ph, ww, wh);
            app.draw(g);
            g.end();
            std::string path = std::string("/tmp/opencode/shot_") + name + ".png";
            saveShot(ren, path.c_str());
            SDL_RenderPresent(ren);
        };

        auto warm = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                menu::updateCore(app.ui, 1.f / 60.f);
                app.update(1.f / 60.f);
            }
        };

        app.cfg.worldMode = 2;
        app.db.scan();
        if (app.db.count() == 0) {
            std::fprintf(stderr, "shots: no maps in songs/\n");
            cleanup();
            return 1;
        }

        app.mouseMoved(720.f, 300.f);
        app.ui.intro = 4.f;

        // Выбор карты.
        app.screen = App::Screen::Select;
        app.sel = 0;
        app.cfg.staticWorld = 0;
        app.ui.styleT = 0.f;
        app.loadCoverFor(0);
        warm(70);
        snap("select");

        // Настройки.
        app.screen = App::Screen::Settings;
        app.setCat = 0;
        app.setRow = 2;
        app.cfg.staticWorld = 1;
        app.ui.styleT = 1.0f;
        app.mouseMoved(700.f, 330.f);
        warm(60);
        snap("settings");

        app.setCat = 1;
        app.setRow = 2;
        warm(40);
        snap("settings_play");

        // Создание карты.
        app.screen = App::Screen::Create;
        app.createSrc = 0;
        app.cfg.staticWorld = 2;
        app.ui.styleT = 2.0f;
        app.mouseMoved(760.f, 250.f);
        warm(60);
        snap("create");

        // Запись карты.
        app.play = game::State{};
        app.play.title = app.db.entries[0].title();
        app.play.diff = app.createName;
        app.play.bpm = app.db.entries[0].map.bpm;
        app.play.offsetBeatMs = app.db.entries[0].map.offsetMs;
        app.play.startMs = 0.0;
        app.play.endMs = app.db.entries[0].lengthMs;
        app.play.songMs = 34200.0;
        app.play.arcAngle = -0.4f;
        app.creating = true;
        app.recorded.clear();
        for (int i = 0; i < 7; ++i) {
            game::Spawn n;
            n.timeMs = 3000.0 + i * 1600.0;
            n.angle = -2.2f + i * 0.55f;
            n.key = i % 3 == 2 ? 1 : 0;
            app.recorded.push_back(n);
        }
        app.recCount = 0.6f;
        snap("record");
        app.creating = false;

        // Геймплей: сдвигаем время и рисуем сцену с историей.
        app.cfg.staticWorld = 0;
        app.ui.styleT = 0.0f;
        app.startMap(0);
        if (app.screen == App::Screen::Play) {
            app.audioStarted = true;
            game::State &gs = app.play;
            size_t idx = std::min<size_t>(14, gs.notes.size() - 1);
            gs.songMs = gs.notes[idx].timeMs - 260.0;
            gs.arcAngle = gs.notes[idx].angle;
            gs.targetAngle = gs.arcAngle;
            size_t hitn = std::min<size_t>(8, idx);
            for (size_t i = 0; i < hitn; ++i) {
                gs.hit[i] = 1;
                gs.judge[i] = game::kPerfect;
                gs.counts[game::kPerfect]++;
                gs.combo++;
                gs.score += 300 + i * 2;
            }
            gs.alive = hitn;
            gs.score = 48210;
            gs.maxCombo = gs.combo;
            gs.lastJudge = game::kPerfect;
            gs.judgeT = 0.08f;
            gs.flash = 1.f;
            gs.bursts.push_back({gs.notes[hitn - 1].angle, 0.3f, 0});
            menu::updateCore(app.ui, 1.f / 60.f);
            snap("play");
        } else {
            std::fprintf(stderr, "shots: no playable map: %s\n",
                         app.errorText.c_str());
        }

        // Пауза.
        if (app.screen == App::Screen::Play) {
            app.paused = true;
            app.mouseMoved(640.f, 350.f);
            app.pauseHover[0] = 1.f;
            snap("pause");
            app.paused = false;
        }

        // Результаты.
        song::stop();
        app.screen = App::Screen::Results;
        app.cfg.staticWorld = 1;
        app.frozenWorld = 1;
        app.ui.styleT = 1.0f;
        app.resClear = true;
        app.resAcc = 0.94;
        app.resScore = 298420;
        app.resMaxCombo = 301;
        app.resCounts[0] = 468;
        app.resCounts[1] = 40;
        app.resCounts[2] = 4;
        app.resCounts[3] = 8;
        app.resNewBest = true;
        app.resBest.valid = true;
        app.resBest.score = 298420;
        app.resBest.acc = 0.94;
        app.resBest.plays = 4;
        app.resDeltas.clear();
        for (int i = 0; i < 160; ++i) {
            float d = std::sin(float(i) * 2.17f) * 34.f + 7.f +
                      float((i % 9) - 4) * 3.5f;
            app.resDeltas.push_back(d);
        }
        app.resMeanDelta = 7.f;
        app.resTitle = app.db.count() > 0 ? app.db.entries[0].title() : "SONG";
        app.resDiff = app.db.count() > 0 ? app.db.entries[0].map.version : "DIFF";
        app.mouseMoved(560.f, 630.f);
        for (int i = 0; i < 3; ++i) {
            app.resHover[i] = (i == 0) ? 1.f : 0.f;
        }
        warm(40);
        snap("results");

        cleanup();
        return 0;
    }

    bool running = true;
    float mouseWX = 640.f, mouseWY = 360.f;
    float volume = app.cfg.master;
    Uint64 prev = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouseWX = e.motion.x;
                mouseWY = e.motion.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                mouseWX = e.button.x;
                mouseWY = e.button.y;
                break;
            default:
                break;
            }

            if (e.type == SDL_EVENT_QUIT) {
                continue;
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                Pt m = g.windowToLogical(mouseWX, mouseWY);
                app.mouseMoved(m.x, m.y);
            }

            if (e.type == SDL_EVENT_MOUSE_WHEEL &&
                app.screen == App::Screen::Menu) {
                volume = std::clamp(volume + e.wheel.y * 0.05f, 0.f, 1.f);
                app.cfg.master = volume;
                app.cfg.save(paths::settingsFile());
                menu::toast(app.ui,
                            "VOLUME " +
                                std::to_string(int(volume * 100.f + 0.5f)) + "%");
                continue;
            }

            app.handleEvent(e);
            if (app.quit) {
                running = false;
            }
        }

        Uint64 now = SDL_GetTicks();
        float dt = std::min(0.05f, float(now - prev) / 1000.f);
        prev = now;

        int pw = 0, ph = 0, ww = 0, wh = 0;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        SDL_GetWindowSize(win, &ww, &wh);
        if (pw <= 0 || ph <= 0) {
            SDL_Delay(10);
            continue;
        }

        Pt m = g.windowToLogical(mouseWX, mouseWY);
        app.mouseMoved(m.x, m.y);

        app.update(dt);

        if (app.wantFullscreenApply) {
            SDL_SetWindowFullscreen(win, app.cfg.fullscreen);
            app.wantFullscreenApply = false;
        }

        // Музыка меню: процедурные биты играют на всех «менюшных» экранах,
        // в игре и записи — тишина, чтобы не мешать песне.
        if (app.menuMusicActive()) {
            float sw[3];
            styleWeights(app.ui.styleT, sw);
            audio::setWeights(sw[0], sw[1], sw[2]);
        } else {
            audio::setWeights(0.f, 0.f, 0.f);
        }

        if (audio::running()) {
            audio::Levels lv = audio::levels();
            app.ui.beat = lv.pulse;
            app.ui.bandLow = lv.low;
            app.ui.bandMid = lv.mid;
            app.ui.bandHigh = lv.high;
            app.ui.beatPhase = lv.beatPhase;
            app.ui.beatIndex = lv.beatIndex;
            app.ui.bpm = lv.bpm;
        } else {
            syntheticBeat(app.ui);
        }

        for (int k = 0; k < 3; ++k) {
            audio::setBandGain(k, app.ui.mute[k] ? 0.f : 1.f);
        }
        if (app.ui.requestBarRestart) {
            audio::restartBar();
            app.ui.requestBarRestart = false;
        }

        g.begin(pw, ph, ww, wh);
        app.draw(g);
        g.end();
        SDL_RenderPresent(ren);
    }

    cleanup();
    return 0;
}