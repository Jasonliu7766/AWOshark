#pragma once
// MAX98357A I2S 功放 + 喇叭：程序现场合成多种生物音效（和 tuner-sfx.js / mp v2 完全一致）
// 非阻塞：后台任务按队列播放，游戏主循环不卡。
#include <ESP_I2S.h>
#include "config.h"

int spkVolume = SPK_VOLUME;        // 串口 vol<n> 可临时改

enum SfxId { SFX_ROAR, SFX_SCREAM, SFX_CLAP, SFX_PURR, SFX_ANGER, SFX_BOOT };

struct Speaker {
  I2SClass i2s{I2S_NUM_1};          // 用 I2S1；I2S0 给 PDM 麦克风
  bool started = false;
  volatile bool playing = false;
  volatile uint32_t req = 0;        // 每请求 +1，任务看到变化就播
  volatile int reqId = SFX_ROAR;
  static const int RATE = 16000;

  struct Part { float ratio, lvl; };

  // 通用生物发声：多谐波 + 次谐波 + 喉音调幅 + 噪声嘶哑 + 过载失真 + 音高滑动/颤音
  void tone2(float f0, float f1, int ms, const Part *parts, int np,
             float growlHz, float growlD, float noiseMix, float drive, float amp,
             float vibHz, float vibD, float pcurve, float atk, float rel) {
    int total = RATE * ms / 1000; if (total < 1) total = 1;
    static int16_t buf[256 * 2];
    float ph[4] = {0, 0, 0, 0}, gph = 0, nz = 0;
    float vol = spkVolume / 100.0f;
    for (int i = 0; i < total;) {
      int n = 0;
      for (; n < 256 && i < total; n++, i++) {
        float t = (float)i / total;
        float f = f0 + (f1 - f0) * powf(t, pcurve);
        if (vibD) f *= 1.0f + vibD * sinf(2 * PI * vibHz * i / RATE);
        float s = 0;
        for (int p = 0; p < np; p++) { ph[p] += f * parts[p].ratio / RATE; s += parts[p].lvl * sinf(2 * PI * ph[p]); }
        if (growlD) { gph += growlHz / RATE; s *= (1 - growlD) + growlD * (0.6f + 0.4f * sinf(2 * PI * gph)) * 2; }
        if (noiseMix) { float r = ((int32_t)esp_random() >> 16) / 32768.0f; nz += 0.3f * (r - nz); s = s * (1 - noiseMix) + nz * noiseMix * 3; }
        s *= drive; s = s > 1 ? 1 : (s < -1 ? -1 : s);
        float env = t < atk ? t / atk : (t > 1 - rel ? (1 - t) / rel : 1);
        int16_t x = (int16_t)constrain(s * amp * env * vol * 26000.0f, -32767.0f, 32767.0f);
        buf[2 * n] = buf[2 * n + 1] = x;
      }
      i2s.write((uint8_t *)buf, n * 4);
    }
  }
  void silence(int ms) {
    static int16_t z[256 * 2] = {0};
    int total = RATE * ms / 1000;
    while (total > 0) { int c = total > 256 ? 256 : total; i2s.write((uint8_t *)z, c * 4); total -= c; }
  }
  // SOUND_RENDER_PLACEHOLDER
  void doRender(int id) {
    static const Part P_SCREAM1[] = {{1, 1}, {2.01f, .5f}};
    static const Part P_SCREAM2[] = {{1, 1}, {1.01f, .7f}, {.5f, .4f}};
    static const Part P_ROAR[]    = {{1, 1}, {.5f, .8f}, {2, .3f}};
    static const Part P_ANGER[]   = {{1, 1}, {1.414f, .9f}};
    static const Part P_BOOT[]    = {{1, 1}, {1.5f, .5f}, {2, .3f}};
    static const Part P_PURR[]    = {{1, 1}, {2, .4f}};
    static const Part P_CLAP[]    = {{1, 1}, {1.5f, .4f}};
    switch (id) {
      case SFX_SCREAM:
        tone2(1500, 1300, 90,  P_SCREAM1, 2, 0, 0, .25f, 2.2f, .9f, 16, .08f, 1, .005f, .10f);
        tone2(1250, 520, 430,  P_SCREAM2, 3, 0, 0, .30f, 2.6f, 1.0f, 13, .10f, 1.6f, .004f, .25f);
        break;
      case SFX_ROAR:
        tone2(150, 60, 900, P_ROAR, 3, 48, .7f, .25f, 2.8f, 1.0f, 5, .06f, 1.3f, .01f, .35f);
        break;
      case SFX_ANGER:
        for (int k = 0; k < 3; k++) { tone2(440, 440, 130, P_ANGER, 2, 20, .5f, 0, 2.0f, .85f, 0, 0, 1, .005f, .10f); silence(45); }
        break;
      case SFX_BOOT:
        tone2(70, 180, 700, P_BOOT, 3, 12, .3f, 0, 1.6f, .7f, 0, 0, 1, .08f, .30f);
        break;
      case SFX_PURR:
        tone2(85, 100, 700, P_PURR, 2, 26, .6f, .10f, 1.4f, .55f, 0, 0, 1, .05f, .30f);
        break;
      case SFX_CLAP:
        tone2(300, 700, 160, P_CLAP, 2, 30, .4f, 0, 1.8f, .6f, 10, .05f, 1, .01f, .20f);
        break;
    }
  }
  static void task(void *arg) {
    Speaker *s = (Speaker *)arg;
    uint32_t seen = 0;
    for (;;) {
      if (s->req == seen) { vTaskDelay(10); continue; }
      seen = s->req;
      s->playing = true;
      s->doRender(s->reqId);
      s->playing = false;
    }
  }
  void begin() {
#if USE_SPK
    i2s.setPins(PIN_SPK_BCLK, PIN_SPK_LRC, PIN_SPK_DIN);
    started = i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    Serial.printf("[spk] I2S begin %s\n", started ? "ok" : "FAILED");
    if (started) xTaskCreatePinnedToCore(task, "spk", 4096, this, 2, nullptr, 0);
#endif
  }
  void play(SfxId id) { if (started) { reqId = id; req++; } }
  void roar() { play(SFX_ROAR); }     // 兼容原调用

};
