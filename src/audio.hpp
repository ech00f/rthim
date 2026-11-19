#pragma once

namespace audio {

// Открывает устройство и запускает процедурный микс трёх битов.
// Возвращает false, если аудио недоступно — меню тогда живёт на
// синтетическом пульсе.
bool init();
void shutdown();
bool running();

// Веса трёх миров (0..2): громкость каждого лупа. Меняются плавно,
// поэтому переход между стилями — это кроссфейд, а не щелчок.
void setWeights(float w0, float w1, float w2);

struct Levels {
    float pulse = 0.f;    // огибающая бочки 0..1 — по ней бьётся интерфейс
    float low = 0.f;      // низ
    float mid = 0.f;      // середина
    float high = 0.f;     // верх
    float beatPhase = 0.f; // 0..1 внутри доли
    int beatIndex = 0;    // 0..3 — какая доля в такте
    int bpm = 128;
};

Levels levels();

int styleBpm(int index);

// Офлайн-рендер трёх лупов в WAV (послушать и проверить без устройства).
bool bounce(const char *dir, float seconds);

} // namespace audio