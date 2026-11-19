#include "app.hpp"

#include "audio.hpp"
#include "paths.hpp"
#include "screens.hpp"
#include "scores.hpp"
#include "settings_ui.hpp"
#include "ui.hpp"
#include "song.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

namespace fs = std::filesystem;

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

std::string slug(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "map";
    }
    return out;
}

} // namespace

bool App::init(Gfx &g) {
    gfx = &g;
    cfg.load(paths::settingsFile());
    scores::load(paths::scoresFile());
    db.scan();
    resortMaps();

    // На старте показываем самую свежую карту из истории.
    {
        int recent = -1;
        long long newest = 0;
        for (int i = 0; i < db.count(); ++i) {
            const ScoreEntry *e = scores::get(db.entries[size_t(i)].id);
            if (e && e->valid && e->lastPlayed > newest) {
                newest = e->lastPlayed;
                recent = i;
            }
        }
        if (recent >= 0) {
            sel = recent;
        }
    }

    song::setMusicVolume(cfg.master * cfg.music);
    song::setSfxVolume(cfg.master * cfg.sfx);
    audio::setVolume(cfg.master);

    ui.worldMode = cfg.worldMode;
    ui.staticWorld = cfg.staticWorld;
    ui.autoSpeed = cfg.autoSpeed;
    ui.intro = 0.f;
    screen = Screen::Menu;
    return true;
}

bool App::menuMusicActive() const {
    if (screen == Screen::Menu || screen == Screen::Select ||
        screen == Screen::Settings) {
        return true;
    }
    if (screen == Screen::Create && !creating) {
        return true;
    }
    return false;
}

void App::mouseMoved(float x, float y) {
    mouseX = x;
    mouseY = y;
    ui.rawX = x;
    ui.rawY = y;
}

void App::go(Screen s) {
    screen = s;
    ui.pointerDown = false;
    mouseDown = false;
}

void App::openSelect() {
    if (db.count() > 0) {
        sel = std::clamp(sel, 0, db.count() - 1);
    }
    go(Screen::Select);
}

void App::openSettings(int cat) {
    setCat = std::clamp(cat, 0, setui::catCount() - 1);
    setRow = 0;
    go(Screen::Settings);
}

void App::openCreate() {
    createSrc = std::clamp(sel, 0, std::max(0, db.count() - 1));
    creating = false;
    recorded.clear();
    nameEdit = false;
    createMsg.clear();
    go(Screen::Create);
}

void App::update(float dt) {
    dt = std::min(dt, 0.05f);

    // Мир меню: в игре и результатах он заморожен, иначе курсор,
    // бегающий по кольцу, перекрашивал бы весь экран.
    bool freeze = (screen == Screen::Play) ||
                  (screen == Screen::Results) ||
                  (screen == Screen::Create && creating);
    if (freeze) {
        ui.worldMode = 2;
        ui.staticWorld = frozenWorld;
    } else {
        ui.worldMode = cfg.worldMode;
        ui.staticWorld = cfg.staticWorld;
        ui.autoSpeed = cfg.autoSpeed;
    }

    if (screen == Screen::Menu) {
        menu::update(ui, dt);
    } else {
        menu::updateCore(ui, dt);
    }

    if (errorT > 0.f) {
        errorT = std::max(0.f, errorT - dt);
    }

    switch (screen) {
    case Screen::Menu:
        break;
    case Screen::Select:
        updateSelect(dt);
        break;
    case Screen::Settings:
        updateSettings(dt);
        break;
    case Screen::Create:
        updateCreate(dt);
        break;
    case Screen::Play:
        updatePlay(dt);
        break;
    case Screen::Results:
        updateResults(dt);
        break;
    }

    audio::setVolume(cfg.master);
    song::setMusicVolume(cfg.master * cfg.music);
    song::setSfxVolume(cfg.master * cfg.sfx);
}

void App::updateMenu(float) {}

void App::updateSelect(float dt) {
    if (db.count() == 0) {
        return;
    }
    sel = std::clamp(sel, 0, db.count() - 1);

    const float rowH = 78.f;
    float maxScroll =
        std::max(0.f, float(db.count()) * rowH - layout::kSelectList.h + 8.f);
    float target = float(sel) * rowH - layout::kSelectList.h * 0.42f;
    target = std::clamp(target, 0.f, maxScroll);

    selScrollV += (target - selScroll) * 90.f * dt;
    selScrollV *= std::exp(-dt * 9.f);
    selScroll += selScrollV * dt * 60.f;
    selScroll = std::clamp(selScroll, 0.f, maxScroll);

    Rect list = layout::kSelectList;
    selHover = -1;
    if (list.contains(mouseX, mouseY)) {
        float local = mouseY - list.y + selScroll;
        int i = int(local / rowH);
        if (i >= 0 && i < db.count()) {
            selHover = i;
        }
    }
    for (int i = 0; i < db.count() && i < 32; ++i) {
        float target2 = (i == selHover || i == sel) ? 1.f : 0.f;
        selHoverAnim[i] = ::ui::approach(selHoverAnim[i], target2, dt, 16.f);
    }
    Rect pb = layout::selectPlay();
    Rect cb = layout::selectCreate();
    Rect bb = layout::selectBack();
    selBtnHover[0] = ::ui::approach(selBtnHover[0], pb.contains(mouseX, mouseY) ? 1.f : 0.f, dt, 18.f);
    selBtnHover[1] = ::ui::approach(selBtnHover[1], cb.contains(mouseX, mouseY) ? 1.f : 0.f, dt, 18.f);
    selBtnHover[2] = ::ui::approach(selBtnHover[2], bb.contains(mouseX, mouseY) ? 1.f : 0.f, dt, 18.f);
}

void App::updateSettings(float dt) {
    setHoverCat = layout::settingsCatAt(mouseX, mouseY);
    setHoverRow = layout::settingsRowAt(mouseX, mouseY);
    auto list = setui::rowsFor(setCat, cfg);
    if (setHoverRow >= list.n) {
        setHoverRow = -1;
    }
    for (int i = 0; i < setui::catCount(); ++i) {
        catHover[i] = ::ui::approach(catHover[i],
                                   (i == setHoverCat || i == setCat) ? 1.f : 0.f,
                                   dt, 18.f);
    }
    for (int i = 0; i < 12; ++i) {
        rowHover[i] = ::ui::approach(rowHover[i],
                                   (i == setHoverRow || i == setRow) ? 1.f : 0.f,
                                   dt, 18.f);
    }

    if (setDrag >= 0 && mouseDown) {
        setSlider(setDrag, mouseX);
    }
    if (setDrag >= 0 && !mouseDown) {
        setDrag = -1;
        cfg.save(paths::settingsFile());
    }

}

void App::updateCreate(float dt) {
    if (createMsgT > 0.f) {
        createMsgT = std::max(0.f, createMsgT - dt);
    }
    if (!creating) {
        createHover = -1;
        if (layout::kCreateList.contains(mouseX, mouseY)) {
            float local = mouseY - layout::kCreateList.y + createScroll;
            int i = int(local / 64.f);
            if (i >= 0 && i < db.count()) {
                createHover = i;
            }
        }
        for (int i = 0; i < db.count() && i < 32; ++i) {
            createHoverAnim[i] =
                ::ui::approach(createHoverAnim[i],
                             (i == createHover || i == createSrc) ? 1.f : 0.f,
                             dt, 16.f);
        }
        float maxScroll = std::max(
            0.f, float(db.count()) * 64.f - layout::kCreateList.h + 8.f);
        float target =
            std::clamp(float(createSrc) * 64.f - layout::kCreateList.h * 0.4f,
                       0.f, maxScroll);
        createScroll += (target - createScroll) * (1.f - std::exp(-dt * 12.f));
        Rect sb = layout::createStart();
        Rect bb = layout::createBack();
        createBtnHover[0] = ::ui::approach(
            createBtnHover[0], sb.contains(mouseX, mouseY) ? 1.f : 0.f, dt,
            18.f);
        createBtnHover[1] = ::ui::approach(
            createBtnHover[1], bb.contains(mouseX, mouseY) ? 1.f : 0.f, dt,
            18.f);
        return;
    }

    // --- запись -----------------------------------------------------------
    double pos = song::positionMs();
    game::aimAt(play, mouseX, mouseY);
    play.songMs = pos;
    play.offsetMs = 0.0;

    // Метроном: короткий тик на каждой доле.
    if (cfg.metronome && play.bpm > 1.0) {
        double beat = 60000.0 / play.bpm;
        long long idx =
            (long long)std::floor((pos - play.offsetBeatMs) / beat);
        if (idx != metroBeat) {
            metroBeat = idx;
            song::sfx(3);
        }
    }
    float k = 1.f - std::exp(-dt * 32.f);
    float d = play.targetAngle - play.arcAngle;
    d = std::atan2(std::sin(d), std::cos(d));
    play.arcAngle += d * k;

    if (pendingRec >= 0) {
        double when = snapTime(pos);
        double minGap = cfg.mapSnap
                            ? std::max(90.0, 60000.0 / std::max(1.0, play.bpm) / 8.0)
                            : 150.0;
        if (when - lastRecMs >= minGap) {
            game::Spawn n;
            n.timeMs = when;
            n.angle = play.arcAngle;
            n.key = pendingRec;
            recorded.push_back(n);
            lastRecMs = when;
            recCount = 1.f;
            if (cfg.hitSound) {
                song::sfx(pendingRec);
            }
        }
        pendingRec = -1;
    }
    recCount = std::max(0.f, recCount - dt * 4.f);

    if (song::finished()) {
        saveCustom();
    }
}

void App::updatePlay(float dt) {
    if (paused) {
        for (int i = 0; i < 3; ++i) {
            Rect r = layout::pauseButton(i);
            pauseHover[i] = ::ui::approach(
                pauseHover[i], r.contains(mouseX, mouseY) ? 1.f : 0.f, dt,
                18.f);
        }
        return;
    }

    game::aimAt(play, mouseX, mouseY);

    if (!audioStarted) {
        leadLeft -= dt;
        double t = audioStartMs - double(std::max(0.f, leadLeft)) * 1000.0;
        if (leadLeft <= 0.f) {
            audioStarted = true;
            t = audioStartMs;
            song::play(audioStartMs);
        }
        game::update(play, t, dt, cfg);
        return;
    }

    int missBefore = play.counts[game::kMiss];
    double pos = song::positionMs();
    game::update(play, pos, dt, cfg);

    if (cfg.autoplay && !play.finished) {
        for (size_t i = play.alive; i < play.notes.size(); ++i) {
            if (play.hit[i] != 0) {
                continue;
            }
            const game::Spawn &n = play.notes[i];
            double d = (pos - double(cfg.offsetMs)) - n.timeMs;
            if (d < -600.0) {
                break;
            }
            if (std::fabs(d) <= 30.0) {
                play.arcAngle = n.angle;
                play.targetAngle = n.angle;
                if (game::press(play, n.key, cfg) && cfg.hitSound) {
                    song::sfx(n.key);
                }
            }
            break;
        }
    }

    if (play.counts[game::kMiss] > missBefore && cfg.hitSound) {
        song::sfx(2);
    }

    if (play.finished) {
        finishMap(play.counts[game::kMiss] * 3 < play.totalNotes() &&
                  play.accuracy() > 0.55);
    } else if (song::finished()) {
        play.finished = true;
        finishMap(play.counts[game::kMiss] * 3 < play.totalNotes() &&
                  play.accuracy() > 0.55);
    }
}

void App::updateResults(float dt) {
    for (int i = 0; i < 3; ++i) {
        Rect r = layout::resultsButton(i);
        resHover[i] = ::ui::approach(
            resHover[i], r.contains(mouseX, mouseY) ? 1.f : 0.f, dt, 18.f);
    }
}

void App::startMap(int index) {
    if (index < 0 || index >= db.count()) {
        return;
    }
    mapdb::Entry &e = db.entries[size_t(index)];
    std::string err;

    if (!db.ensureAudio(e, err)) {
        errorText = "AUDIO: " + err;
        errorT = 4.f;
        return;
    }
    if (loadedPath != e.audioPath || !song::loaded()) {
        if (!song::load(e.audioPath, err)) {
            errorText = "SONG: " + err;
            errorT = 4.f;
            return;
        }
    }

    std::vector<game::Spawn> notes;
    notes.reserve(e.map.objects.size());
    for (const beatmap::HitObject &o : e.map.objects) {
        if (o.type == 2 || o.timeMs < 0.0) {
            continue;
        }
        game::Spawn s;
        s.timeMs = o.timeMs;
        s.angle = o.angle;
        s.key = (o.type == 1) ? 1 : 0;
        notes.push_back(s);
    }
    if (notes.empty()) {
        errorText = "В КАРТЕ НЕТ ИГРАБЕЛЬНЫХ НОТ";
        errorT = 3.f;
        return;
    }
    std::sort(notes.begin(), notes.end(),
              [](const game::Spawn &a, const game::Spawn &b) {
                  return a.timeMs < b.timeMs;
              });

    // Небольшой отсчёт перед стартом: у карт первая нота может быть
    // почти сразу (как в примере), и без него первый метеорит не поймать.
    audioStartMs = std::max(0.0, notes.front().timeMs - 1500.0);
    leadLeft = std::clamp(cfg.leadInSec, 0.5f, 6.f);
    audioStarted = false;
    double startMs = audioStartMs - double(leadLeft) * 1000.0;
    game::start(play, notes, startMs, e.map.bpm, e.map.offsetMs, cfg);
    play.title = e.title();
    play.diff = e.map.version;
    play.endMs = std::max(song::durationMs(), notes.back().timeMs);

    playMap = index;
    loadedPath = e.audioPath;
    frozenWorld = std::clamp(int(std::lround(ui.styleT)), 0, 2);
    paused = false;
    pauseSel = 0;
    loadCoverFor(index);
    go(Screen::Play);

    song::stop();
}

void App::retryMap() {
    if (playMap >= 0) {
        startMap(playMap);
    }
}

void App::finishMap(bool clear) {
    if (screen != Screen::Play) {
        return;
    }
    song::stop();
    resClear = clear;
    resAcc = play.accuracy();
    resScore = play.score;
    resMaxCombo = play.maxCombo;
    for (int i = 0; i < 4; ++i) {
        resCounts[i] = play.counts[i];
    }
    resTitle = play.title;
    resDiff = play.diff;
    resSel = 0;

    // Лучший результат и статистика тайминга.
    resNewBest = false;
    resBest = ScoreEntry{};
    if (playMap >= 0 && playMap < db.count()) {
        const std::string &mapId = db.entries[size_t(playMap)].id;
        bool complete = play.resolvedNotes() == play.totalNotes();
        if (!cfg.autoplay && complete) {
            ScoreEntry before;
            resNewBest = scores::submit(mapId, play.score, play.accuracy(),
                                        play.maxCombo, play.counts, resBest,
                                        before);
            scores::save(paths::scoresFile());
        } else {
            // Демо/недоигранный прогон: показываем прежний рекорд, но не пишем.
            const ScoreEntry *prev = scores::get(mapId);
            if (prev && prev->valid) {
                resBest = *prev;
            }
        }
    }
    resDeltas = play.hitDelta;
    double sum = 0.0;
    int n = 0;
    for (float d : resDeltas) {
        if (std::fabs(d) < 1e8f) {
            sum += double(d);
            ++n;
        }
    }
    resMeanDelta = (n > 0) ? float(sum / double(n)) : 0.f;

    go(Screen::Results);
}

const char *App::sortName() const {
    static const char *names[5] = {"ARTIST", "TITLE", "BPM", "LENGTH",
                                   "RECENT"};
    return names[std::clamp(sortMode, 0, 4)];
}

void App::resortMaps() {
    if (db.entries.empty()) {
        sel = 0;
        return;
    }
    int old = std::clamp(sel, 0, db.count() - 1);
    std::string keep = db.entries[size_t(old)].sourcePath;

    auto lower = [](std::string v) {
        for (char &c : v) {
            c = char(std::tolower(static_cast<unsigned char>(c)));
        }
        return v;
    };
    auto byArtist = [&](const mapdb::Entry &a, const mapdb::Entry &b) {
        std::string aa = lower(a.artist());
        std::string bb = lower(b.artist());
        if (aa != bb) {
            return aa < bb;
        }
        return lower(a.title()) < lower(b.title());
    };

    switch (sortMode) {
    case 1:
        std::stable_sort(db.entries.begin(), db.entries.end(),
                         [&](const mapdb::Entry &a, const mapdb::Entry &b) {
                             std::string aa = lower(a.title());
                             std::string bb = lower(b.title());
                             if (aa != bb) {
                                 return aa < bb;
                             }
                             return byArtist(a, b);
                         });
        break;
    case 2:
        std::stable_sort(db.entries.begin(), db.entries.end(),
                         [&](const mapdb::Entry &a, const mapdb::Entry &b) {
                             if (a.map.bpm != b.map.bpm) {
                                 return a.map.bpm < b.map.bpm;
                             }
                             return byArtist(a, b);
                         });
        break;
    case 3:
        std::stable_sort(db.entries.begin(), db.entries.end(),
                         [&](const mapdb::Entry &a, const mapdb::Entry &b) {
                             if (a.lengthMs != b.lengthMs) {
                                 return a.lengthMs < b.lengthMs;
                             }
                             return byArtist(a, b);
                         });
        break;
    case 4:
        std::stable_sort(db.entries.begin(), db.entries.end(),
                         [&](const mapdb::Entry &a, const mapdb::Entry &b) {
                             const ScoreEntry *sa = scores::get(a.id);
                             const ScoreEntry *sb = scores::get(b.id);
                             long long ta = (sa && sa->valid) ? sa->lastPlayed : 0;
                             long long tb = (sb && sb->valid) ? sb->lastPlayed : 0;
                             if (ta != tb) {
                                 return ta > tb;
                             }
                             return byArtist(a, b);
                         });
        break;
    default:
        std::stable_sort(db.entries.begin(), db.entries.end(), byArtist);
        break;
    }

    sel = 0;
    for (int i = 0; i < db.count(); ++i) {
        if (db.entries[size_t(i)].sourcePath == keep) {
            sel = i;
            break;
        }
    }
    sel = std::clamp(sel, 0, db.count() - 1);
}

void App::loadCoverFor(int index) {
    if (index < 0 || index >= db.count()) {
        return;
    }
    mapdb::Entry &e = db.entries[size_t(index)];
    if (e.cover) {
        return;
    }
    // Держим не больше двух обложек в памяти.
    int kept = 0;
    for (int i = 0; i < db.count(); ++i) {
        if (db.entries[i].cover && i != index) {
            ++kept;
            if (kept > 2) {
                db.dropCover(db.entries[i]);
            }
        }
    }
    if (gfx) {
        db.ensureCover(e, *gfx);
    }
}

double App::snapTime(double pos) const {
    if (!cfg.mapSnap || play.bpm <= 1.0) {
        return pos;
    }
    double div = 60000.0 / play.bpm / 4.0; // 1/16 ноты
    double base = play.offsetBeatMs;
    double snapped = base + std::round((pos - base) / div) * div;
    if (std::fabs(snapped - pos) <= 120.0) {
        return snapped;
    }
    return pos;
}

void App::saveCustom() {
    if (recorded.empty()) {
        creating = false;
        song::stop();
        createMsg = "НЕТ НОТ — ЗАПИСЬ ОТМЕНЕНА";
        createMsgT = 3.f;
        return;
    }
    if (createSrc < 0 || createSrc >= db.count()) {
        creating = false;
        song::stop();
        return;
    }
    mapdb::Entry &src = db.entries[size_t(createSrc)];
    std::string err;
    if (!db.ensureAudio(src, err)) {
        creating = false;
        song::stop();
        createMsg = "AUDIO: " + err;
        createMsgT = 3.f;
        return;
    }

    std::string base = slug(createName);
    std::string dir = (fs::path(paths::customSongs()) / base).string();
    std::error_code ec;
    for (int n = 2; fs::exists(dir, ec) && n < 99; ++n) {
        dir = (fs::path(paths::customSongs()) /
               (base + "_" + std::to_string(n)))
                  .string();
    }
    fs::create_directories(dir, ec);

    fs::path audioSrc(src.audioPath);
    fs::path audioDst = fs::path(dir) / ("audio" + audioSrc.extension().string());
    fs::copy_file(audioSrc, audioDst, fs::copy_options::overwrite_existing, ec);

    std::string title = src.title();
    std::string artist = src.artist();
    double bpm = src.map.bpm > 1.0 ? src.map.bpm : 120.0;
    double offset = src.map.offsetMs;

    std::ofstream f((fs::path(dir) / "map.osu").string());
    if (!f) {
        creating = false;
        song::stop();
        createMsg = "НЕ УДАЛОСЬ ЗАПИСАТЬ ФАЙЛ";
        createMsgT = 3.f;
        return;
    }
    f << "osu file format v14\n\n";
    f << "[General]\nAudioFilename: " << audioDst.filename().string()
      << "\nAudioLeadIn: 0\nPreviewTime: -1\nMode: 0\n\n";
    f << "[Metadata]\nTitle:" << title << "\nTitleUnicode:" << title
      << "\nArtist:" << artist << "\nArtistUnicode:" << artist
      << "\nCreator:RTHIM\nVersion:" << createName
      << "\nSource:\nTags:rthim custom\nBeatmapID:0\nBeatmapSetID:-1\n\n";
    f << "[Difficulty]\nHPDrainRate:5\nCircleSize:4\nOverallDifficulty:8\n"
         "ApproachRate:9\nSliderMultiplier:3\nSliderTickRate:1\n\n";
    f << "[TimingPoints]\n"
      << offset << "," << (60000.0 / bpm) << ",4,2,1,80,1,0\n\n";
    f << "[HitObjects]\n";
    for (const game::Spawn &n : recorded) {
        int x = int(std::lround(256.0 + std::cos(n.angle) * 150.0));
        int y = int(std::lround(192.0 + std::sin(n.angle) * 150.0));
        x = std::clamp(x, 0, 511);
        y = std::clamp(y, 0, 383);
        long t = long(std::lround(n.timeMs));
        if (n.key == 1) {
            int x2 = int(std::lround(256.0 + std::cos(n.angle + 1.1) * 158.0));
            int y2 = int(std::lround(192.0 + std::sin(n.angle + 1.1) * 158.0));
            f << x << "," << y << "," << t << ",2,0,L|" << x2 << ":" << y2
              << ",1,20\n";
        } else {
            f << x << "," << y << "," << t << ",1,0,0:0:0:0:\n";
        }
    }
    f.close();

    creating = false;
    recorded.clear();
    song::stop();
    db.scan();
    resortMaps();

    // На старте показываем самую свежую карту из истории.
    {
        int recent = -1;
        long long newest = 0;
        for (int i = 0; i < db.count(); ++i) {
            const ScoreEntry *e = scores::get(db.entries[size_t(i)].id);
            if (e && e->valid && e->lastPlayed > newest) {
                newest = e->lastPlayed;
                recent = i;
            }
        }
        if (recent >= 0) {
            sel = recent;
        }
    }
    for (int i = 0; i < db.count(); ++i) {
        if (db.entries[i].custom &&
            fs::path(db.entries[i].sourcePath).parent_path() ==
                fs::path(dir)) {
            sel = i;
            break;
        }
    }
    if (db.count() > 0) {
        loadCoverFor(sel);
    }
    createMsg = "СОХРАНЕНО: " + createName;
    createMsgT = 3.f;
    errorText = "СОХРАНЕНО: " + createName;
    errorT = 3.f;
    go(Screen::Select);
}

void App::adjustSetting(int row, int dir) {
    auto list = setui::rowsFor(setCat, cfg);
    if (row < 0 || row >= list.n) {
        return;
    }
    setui::Row &r = list.rows[row];
    bool wasFullscreen =
        (r.bval == &cfg.fullscreen) ? cfg.fullscreen : false;
    setui::adjust(r, dir, cfg);
    if (r.bval == &cfg.fullscreen && wasFullscreen != cfg.fullscreen) {
        wantFullscreenApply = true;
    }
    cfg.save(paths::settingsFile());
}

void App::setSlider(int row, float x) {
    auto list = setui::rowsFor(setCat, cfg);
    if (row < 0 || row >= list.n) {
        return;
    }
    setui::Row &r = list.rows[row];
    if (r.kind != setui::Kind::Slider || !r.fval || !setui::enabled(r, cfg)) {
        return;
    }
    Rect track = layout::settingsSlider(row);
    float t = clamp01((x - track.x) / track.w);
    *r.fval = r.min + (r.max - r.min) * t;
    // шаг не применяем при перетаскивании — значения плавные
}

// --- ввод -----------------------------------------------------------------

void App::clickMenu(float x, float y) {
    int hit = menu::handleClick(ui, x, y);
    if (hit < 0) {
        return;
    }
    menu::activate(ui, hit);
    if (hit == 0) {
        openSelect();
    } else if (hit == 1) {
        openSelect();
    } else if (hit == 2) {
        openSettings(0);
    } else if (hit == 3) {
        quit = true;
    }
}

void App::clickSelect(float x, float y) {
    Rect back = layout::selectBack();
    if (back.contains(x, y)) {
        go(Screen::Menu);
        return;
    }
    Rect play = layout::selectPlay();
    if (play.contains(x, y) && db.count() > 0) {
        startMap(sel);
        return;
    }
    Rect create = layout::selectCreate();
    if (create.contains(x, y)) {
        openCreate();
        return;
    }
    Rect list = layout::kSelectList;
    if (list.contains(x, y)) {
        int i = int((y - list.y + selScroll) / 78.f);
        if (i >= 0 && i < db.count()) {
            sel = i;
            loadCoverFor(i);
        }
    }
    Rect cover = layout::selectCover();
    if (cover.contains(x, y) && db.count() > 0) {
        startMap(sel);
    }
}

void App::clickSettings(float x, float y) {
    Rect back{64.f, 620.f, 160.f, 54.f};
    if (back.contains(x, y)) {
        go(Screen::Menu);
        return;
    }
    int cat = layout::settingsCatAt(x, y);
    if (cat >= 0) {
        setCat = cat;
        setRow = 0;
        setDrag = -1;
        return;
    }
    int row = layout::settingsRowAt(x, y);
    if (row < 0) {
        return;
    }
    auto list = setui::rowsFor(setCat, cfg);
    if (row >= list.n) {
        return;
    }
    setui::Row &r = list.rows[row];
    if (!setui::enabled(r, cfg)) {
        return;
    }
    setRow = row;
    if (r.kind == setui::Kind::Toggle) {
        bool before = r.bval ? *r.bval : false;
        setui::adjust(r, 1, cfg);
        if (r.bval == &cfg.fullscreen && before != cfg.fullscreen) {
            wantFullscreenApply = true;
        }
        cfg.save(paths::settingsFile());
    } else if (r.kind == setui::Kind::Slider) {
        setDrag = row;
        setSlider(row, x);
    } else if (r.kind == setui::Kind::Choice) {
        Rect rr = layout::settingsRow(row);
        int dir = 1;
        if (x >= rr.x + rr.w - 78.f && x < rr.x + rr.w - 44.f) {
            dir = -1;
        }
        adjustSetting(row, dir);
    }
}

void App::clickCreate(float x, float y) {
    if (creating) {
        return;
    }
    Rect back = layout::createBack();
    if (back.contains(x, y)) {
        openSelect();
        return;
    }
    Rect start = layout::createStart();
    if (start.contains(x, y) && db.count() > 0) {
        // начинаем запись
        mapdb::Entry &e = db.entries[size_t(createSrc)];
        std::string err;
        if (!db.ensureAudio(e, err)) {
            createMsg = "AUDIO: " + err;
            createMsgT = 3.f;
            return;
        }
        if (!song::load(e.audioPath, err)) {
            createMsg = "SONG: " + err;
            createMsgT = 3.f;
            return;
        }
        play = game::State{};
        play.bpm = e.map.bpm;
        play.offsetBeatMs = e.map.offsetMs;
        play.title = e.title();
        play.diff = createName;
        play.arcAngle = -1.5708f;
        play.targetAngle = -1.5708f;
        play.songMs = 0.0;
        play.startMs = 0.0;
        play.endMs = song::durationMs();
        recorded.clear();
        recCount = 0.f;
        lastRecMs = -1000.0;
        pendingRec = -1;
        metroBeat = 0;
        creating = true;
        nameEdit = false;
        frozenWorld = std::clamp(int(std::lround(ui.styleT)), 0, 2);
        song::play(0.0);
        return;
    }
    if (layout::createNameBox().contains(x, y)) {
        nameEdit = true;
        return;
    }
    Rect list = layout::kCreateList;
    if (list.contains(x, y)) {
        int i = int((y - list.y + createScroll) / 64.f);
        if (i >= 0 && i < db.count()) {
            createSrc = i;
            nameEdit = false;
        }
    }
}

void App::keySelect(int sc, bool repeat) {
    if (repeat) {
        return;
    }
    if (sc == SDL_SCANCODE_ESCAPE) {
        go(Screen::Menu);
    } else if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
        if (db.count() > 0) {
            sel = (sel + db.count() - 1) % db.count();
            loadCoverFor(sel);
        }
    } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
        if (db.count() > 0) {
            sel = (sel + 1) % db.count();
            loadCoverFor(sel);
        }
    } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
               sc == SDL_SCANCODE_SPACE) {
        startMap(sel);
    } else if (sc == SDL_SCANCODE_C) {
        openCreate();
    } else if (sc == SDL_SCANCODE_R) {
        db.scan();
        resortMaps();
        if (db.count() > 0) {
            loadCoverFor(sel);
        }
        errorText = "MAPS RESCANNED: " + std::to_string(db.count());
        errorT = 2.5f;
    } else if (sc == SDL_SCANCODE_S) {
        sortMode = (sortMode + 1) % 5;
        resortMaps();
        if (db.count() > 0) {
            loadCoverFor(sel);
        }
        errorText = std::string("SORT: ") + sortName();
        errorT = 1.6f;
    }
}

void App::keySettings(int sc, bool repeat) {
    if (sc == SDL_SCANCODE_ESCAPE) {
        go(Screen::Menu);
        return;
    }
    auto list = setui::rowsFor(setCat, cfg);
    if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W || repeat) {
        if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
            setRow = (setRow + list.n - 1) % std::max(1, list.n);
        }
        return;
    }
    if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
        setRow = (setRow + 1) % std::max(1, list.n);
    } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_A) {
        adjustSetting(setRow, -1);
    } else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_D) {
        adjustSetting(setRow, 1);
    } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
               sc == SDL_SCANCODE_SPACE) {
        if (setRow < list.n && list.rows[setRow].kind != setui::Kind::Info) {
            adjustSetting(setRow, 1);
        }
    } else if (sc == SDL_SCANCODE_TAB) {
        setCat = (setCat + 1) % setui::catCount();
        setRow = 0;
    }
}

void App::keyCreate(int sc, bool repeat) {
    if (creating) {
        if (sc == SDL_SCANCODE_ESCAPE) {
            saveCustom();
        } else if (sc == SDL_SCANCODE_BACKSPACE) {
            if (!recorded.empty()) {
                recorded.pop_back();
                lastRecMs = recorded.empty() ? -1000.0
                                             : recorded.back().timeMs;
            }
        } else if (sc == SDL_SCANCODE_G) {
            cfg.mapSnap = !cfg.mapSnap;
            cfg.save(paths::settingsFile());
        } else if (sc == SDL_SCANCODE_M) {
            cfg.metronome = !cfg.metronome;
            cfg.save(paths::settingsFile());
        }
        return;
    }
    if (nameEdit) {
        if (sc == SDL_SCANCODE_BACKSPACE) {
            if (!createName.empty()) {
                createName.pop_back();
            }
            return;
        }
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
            nameEdit = false;
            return;
        }
        if (sc == SDL_SCANCODE_ESCAPE) {
            nameEdit = false;
            return;
        }
    }
    if (sc == SDL_SCANCODE_ESCAPE) {
        openSelect();
    } else if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
        if (db.count() > 0) {
            createSrc = (createSrc + db.count() - 1) % db.count();
        }
    } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
        if (db.count() > 0) {
            createSrc = (createSrc + 1) % db.count();
        }
    } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
               sc == SDL_SCANCODE_SPACE) {
        Rect r = layout::createStart();
        clickCreate(r.x + 4.f, r.y + 4.f);
    }
    (void)repeat;
}

void App::keyPlay(int sc) {
    if (paused) {
        keyPause(sc);
        return;
    }
    if (sc == SDL_SCANCODE_ESCAPE) {
        paused = true;
        if (audioStarted) {
            song::pause();
        }
        pauseSel = 0;
        return;
    }
    if (sc == SDL_SCANCODE_R) {
        retryMap();
        return;
    }
    int k0 = 0, k1 = 0;
    cfg.keys(k0, k1);
    int key = -1;
    if (sc == k0) {
        key = 0;
    } else if (sc == k1) {
        key = 1;
    }
    if (key >= 0) {
        if (game::press(play, key, cfg) && cfg.hitSound) {
            song::sfx(key);
        }
        return;
    }
    if (sc == SDL_SCANCODE_COMMA) {
        // отладка: сдвиг назад
    }
}

void App::keyPause(int sc) {
    if (sc == SDL_SCANCODE_ESCAPE) {
        paused = false;
        if (audioStarted) {
            song::resume();
        }
        return;
    }
    if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
        pauseSel = (pauseSel + 2) % 3;
    } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
        pauseSel = (pauseSel + 1) % 3;
    } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
               sc == SDL_SCANCODE_SPACE) {
        if (pauseSel == 0) {
            paused = false;
            if (audioStarted) {
                song::resume();
            }
        } else if (pauseSel == 1) {
            retryMap();
        } else if (pauseSel == 2) {
            paused = false;
            song::stop();
            openSelect();
        }
    }
}

void App::keyResults(int sc) {
    if (sc == SDL_SCANCODE_ESCAPE) {
        openSelect();
        return;
    }
    if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_A) {
        resSel = (resSel + 2) % 3;
    } else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_D) {
        resSel = (resSel + 1) % 3;
    } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ||
               sc == SDL_SCANCODE_SPACE) {
        if (resSel == 0) {
            retryMap();
        } else if (resSel == 1) {
            openSelect();
        } else {
            go(Screen::Menu);
        }
    }
}

void App::handleEvent(const SDL_Event &e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button != SDL_BUTTON_LEFT) {
            break;
        }
        mouseDown = true;
        switch (screen) {
        case Screen::Menu:
            ui.pointerDown = true;
            clickMenu(mouseX, mouseY);
            break;
        case Screen::Select:
            clickSelect(mouseX, mouseY);
            break;
        case Screen::Settings:
            clickSettings(mouseX, mouseY);
            break;
        case Screen::Create:
            clickCreate(mouseX, mouseY);
            break;
        case Screen::Play:
            if (paused) {
                for (int i = 0; i < 3; ++i) {
                    if (layout::pauseButton(i).contains(mouseX, mouseY)) {
                        pauseSel = i;
                        keyPause(SDL_SCANCODE_RETURN);
                        break;
                    }
                }
            }
            break;
        case Screen::Results:
            for (int i = 0; i < 3; ++i) {
                if (layout::resultsButton(i).contains(mouseX, mouseY)) {
                    resSel = i;
                    keyResults(SDL_SCANCODE_RETURN);
                    break;
                }
            }
            break;
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT) {
            break;
        }
        mouseDown = false;
        if (screen == Screen::Menu) {
            ui.pointerDown = false;
            menu::handleRelease(ui);
        }
        if (screen == Screen::Settings) {
            setDrag = -1;
        }
        break;

    case SDL_EVENT_KEY_DOWN: {
        bool rep = e.key.repeat;
        int sc = int(e.key.scancode);

        if (sc == SDL_SCANCODE_F11 && !rep) {
            cfg.fullscreen = !cfg.fullscreen;
            wantFullscreenApply = true;
            cfg.save(paths::settingsFile());
            break;
        }
        bool editingName = (screen == Screen::Create && nameEdit && !creating);
        if (!editingName &&
            (sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_KP_MINUS ||
             sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS)) {
            bool plus = (sc == SDL_SCANCODE_EQUALS ||
                         sc == SDL_SCANCODE_KP_PLUS);
            cfg.master = std::clamp(cfg.master + (plus ? 0.05f : -0.05f),
                                    0.f, 1.f);
            cfg.save(paths::settingsFile());
            errorText = "VOLUME " +
                        std::to_string(int(cfg.master * 100.f + 0.5f)) + "%";
            errorT = 1.2f;
            break;
        }
        if (rep) {
            break;
        }

        switch (screen) {
        case Screen::Menu: {
            int n = menu::buttonCount();
            if (sc == SDL_SCANCODE_ESCAPE) {
                quit = true;
            } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
                ui.selected = (ui.selected + 1) % n;
            } else if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
                ui.selected = (ui.selected + n - 1) % n;
            } else if (sc == SDL_SCANCODE_RETURN ||
                       sc == SDL_SCANCODE_KP_ENTER ||
                       sc == SDL_SCANCODE_SPACE) {
                int hit = ui.selected;
                menu::activate(ui, hit);
                if (hit == 0 || hit == 1) {
                    openSelect();
                } else if (hit == 2) {
                    openSettings(0);
                } else {
                    quit = true;
                }
            }
            break;
        }
        case Screen::Select:
            keySelect(sc, rep);
            break;
        case Screen::Settings:
            keySettings(sc, rep);
            break;
        case Screen::Create:
            keyCreate(sc, rep);
            if (creating) {
                int k0 = 0, k1 = 0;
                cfg.keys(k0, k1);
                if (sc == k0 || sc == k1) {
                    // ноту ставим в update: здесь только фиксируем нажатие
                    pendingRec = (sc == k0) ? 0 : 1;
                }
            }
            break;
        case Screen::Play:
            keyPlay(sc);
            break;
        case Screen::Results:
            keyResults(sc);
            break;
        }
        break;
    }

    case SDL_EVENT_TEXT_INPUT:
        if (screen == Screen::Create && nameEdit && !creating) {
            for (const char *p = e.text.text; *p; ++p) {
                if (createName.size() < 26 &&
                    (unsigned char)*p >= 32) {
                    createName.push_back(*p);
                }
            }
        }
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (screen == Screen::Select && db.count() > 0) {
            int dir = (e.wheel.y > 0.f) ? -1 : 1;
            sel = (sel + dir + db.count()) % db.count();
            loadCoverFor(sel);
        } else if (screen == Screen::Create && !creating && db.count() > 0) {
            int dir = (e.wheel.y > 0.f) ? -1 : 1;
            createSrc = (createSrc + dir + db.count()) % db.count();
        } else if (screen == Screen::Settings && !mouseDown) {
            // колесо меняет значение строки под курсором
            if (setHoverRow >= 0) {
                setRow = setHoverRow;
            }
            adjustSetting(setRow, (e.wheel.y > 0.f) ? 1 : -1);
        }
        break;

    case SDL_EVENT_DROP_FILE: {
        std::string path = e.drop.data ? e.drop.data : "";
        std::string low = path;
        for (char &c : low) {
            c = char(std::tolower(static_cast<unsigned char>(c)));
        }
        if (low.size() > 4 && low.substr(low.size() - 4) == ".osz") {
            std::error_code ec;
            std::filesystem::path dst =
                std::filesystem::path(paths::songs()) /
                std::filesystem::path(path).filename();
            std::filesystem::copy_file(
                path, dst, std::filesystem::copy_options::overwrite_existing,
                ec);
            if (!ec) {
                if (screen == Screen::Play ||
                    (screen == Screen::Create && creating)) {
                    song::stop();
                    paused = false;
                    audioStarted = false;
                    creating = false;
                }
                db.scan();
                resortMaps();
                sel = 0;
                for (int i = 0; i < db.count(); ++i) {
                    if (db.entries[size_t(i)].sourcePath == dst.string()) {
                        sel = i;
                        break;
                    }
                }
                loadCoverFor(sel);
                errorText = "IMPORTED: " +
                            std::filesystem::path(path).filename().string();
                errorT = 3.f;
                if (screen != Screen::Select) {
                    go(Screen::Select);
                }
            } else {
                errorText = "IMPORT FAILED";
                errorT = 3.f;
            }
        }
        break;
    }

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        mouseDown = false;
        ui.pointerDown = false;
        ui.dragging = false;
        ui.dragKind = 0;
        if (screen == Screen::Play && !paused) {
            paused = true;
            if (audioStarted) {
                song::pause();
            }
        }
        break;

    default:
        break;
    }
}