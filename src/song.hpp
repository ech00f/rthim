#pragma once

#include <string>
#include <vector>

namespace song {

// Загрузить аудиофайл (WAV PCM16/float или MP3) и подготовить к игре.
// Внутри всё приводится к 48000 Hz, stereo, float32, хранится в памяти.
bool load(const std::string &path, std::string &err);
bool loadFromMemory(const std::vector<unsigned char> &bytes,
                    const std::string &nameHint, std::string &err);
void unload();

void play(double fromMs = 0.0);  // начать/продолжить с позиции
void pause();
void resume();
void stop();
bool loaded();
bool playing();          // идёт воспроизведение (не пауза, не конец)
bool paused();
bool finished();         // дошло до конца

// Текущая позиция по аудио-часам (мс). Считается от реально проигранных
// сэмплов: pushedFrames - SDL_GetAudioStreamQueued(stream), /48.
double positionMs();
double durationMs();

void setMusicVolume(float v); // 0..1
void setSfxVolume(float v);   // 0..1
float musicVolume();
float sfxVolume();

// Короткие синтезированные звуки поверх музыки (без файлов).
// key: 0/1 — два разных тона (удары), 2 — промах.
void sfx(int kind);

} // namespace song