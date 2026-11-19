#include "gfx.hpp"
#include "menu.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
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
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--shot") {
            shotMode = true;
        }
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

    Gfx g;
    g.init(ren, findAssets());

    menu::State s;
    s.selected = 0;

    auto cleanup = [&]() {
        g.shutdown();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
    };

    if (shotMode) {
        const struct {
            float x, y;
        } shots[] = {{150.f, 337.f}, {410.f, 415.f}, {640.f, 337.f},
                     {870.f, 493.f}, {1130.f, 337.f}};
        int idx = 0;
        s.intro = 4.f;
        s.time = 2.f;
        for (const auto &sh : shots) {
            s.rawX = sh.x;
            s.rawY = sh.y;
            for (int k = 0; k < 90; ++k) {
                menu::update(s, 1.f / 60.f);
            }

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

    bool running = true;
    bool pendingClick = false;
    float mouseWX = 640.f, mouseWY = 360.f;
    Uint64 prev = SDL_GetTicks();

    auto activate = [&](int index) {
        menu::activate(s, index);
        if (index == menu::buttonCount() - 1) {
            running = false;
        }
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN: {
                SDL_Keycode k = e.key.key;
                int n = menu::buttonCount();
                if (k == SDLK_ESCAPE) {
                    running = false;
                } else if (k == SDLK_DOWN || k == SDLK_S) {
                    s.selected = (s.selected + 1) % n;
                } else if (k == SDLK_UP || k == SDLK_W) {
                    s.selected = (s.selected + n - 1) % n;
                } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER ||
                           k == SDLK_SPACE) {
                    activate(s.selected);
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                mouseWX = e.motion.x;
                mouseWY = e.motion.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    mouseWX = e.button.x;
                    mouseWY = e.button.y;
                    pendingClick = true;
                }
                break;
            default:
                break;
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

        g.begin(pw, ph, ww, wh);

        Pt m = g.windowToLogical(mouseWX, mouseWY);
        s.rawX = m.x;
        s.rawY = m.y;

        menu::update(s, dt);

        if (pendingClick) {
            int hit = menu::hitTest(s.rawX, s.rawY);
            if (hit >= 0) {
                s.selected = hit;
                activate(hit);
            }
            pendingClick = false;
        }

        menu::draw(g, s);
        g.end();
        SDL_RenderPresent(ren);
    }

    cleanup();
    return 0;
}