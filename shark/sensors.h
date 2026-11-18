#pragma once
// 传感器层：每个模块都"掉线自动重连"，读不到时返回无效值，不阻塞游戏。
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include "config.h"

// ===================== 按键板 TM1650（0x24 控制，0x27 读键） =====================
struct Keypad {
  bool ok = false;
  uint8_t last = 0;          // 当前按住的键号，0 = 无
  uint32_t lastTry = 0;

  bool init() {
    for (uint8_t i = 0; i < 4; i++) { Wire.beginTransmission(0x34 + i); Wire.write(0); Wire.endTransmission(); }
    Wire.beginTransmission(0x24); Wire.write(0x11);
    ok = Wire.endTransmission() == 0;
    return ok;
  }
  // 返回新按下的键号 1–16；没有新按键返回 0。按住不重复。
  uint8_t poll() {
#if USE_KEYPAD
    if (!ok) {
      if (millis() - lastTry > 2000) { lastTry = millis(); if (init()) Serial.println("[key] TM1650 ready"); }
      return 0;
    }
    if (Wire.requestFrom((uint8_t)0x27, (uint8_t)1) != 1) { ok = false; Serial.println("[key] lost, retrying"); return 0; }
    uint8_t raw = Wire.read();
    uint8_t k = 0;
    if ((raw & 0x40) && (raw & 0xC4) == 0x44) {
      uint8_t ki = (raw >> 3) & 7, row = raw & 3;
      if (ki < 4) k = 16 - (row * 4 + ki);    // 实测板上编号与芯片顺序相反
    }
    uint8_t pressed = (k && k != last) ? k : 0;
    last = k;
    return pressed;
#else
    return 0;
#endif
  }
};

// ===================== 温湿度 GXHT30C（0x44） =====================
// 非阻塞：先发测量命令，过 20ms 再取结果。
struct TempHum {
  uint8_t addr = 0;
  bool pending = false;
  uint32_t tCmd = 0, lastTry = 0;
  float t = NAN, rh = NAN;
  float baseRh = NAN;        // 湿度基线：往下跟得快，往上跟得很慢
  uint32_t lastOk = 0;

  static uint8_t crc8(const uint8_t *d) {
    uint8_t c = 0xFF;
    for (int i = 0; i < 2; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = (c & 0x80) ? (c << 1) ^ 0x31 : c << 1; }
    return c;
  }
  bool probe(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

  void update(float dt) {
#if USE_TH
    uint32_t now = millis();
    if (!addr) {
      if (now - lastTry > 2000) {
        lastTry = now;
        if (probe(0x44)) addr = 0x44; else if (probe(0x45)) addr = 0x45;
        if (addr) Serial.printf("[th] GXHT30C at 0x%02X\n", addr);
      }
      return;
    }
    if (!pending) {
      Wire.beginTransmission(addr); Wire.write(0x24); Wire.write(0x00);
      if (Wire.endTransmission() != 0) { fail(); return; }
      pending = true; tCmd = now;
    } else if (now - tCmd >= 20) {
      pending = false;
      uint8_t b[6];
      if (Wire.requestFrom(addr, (uint8_t)6) != 6) { fail(); return; }
      for (int i = 0; i < 6; i++) b[i] = Wire.read();
      if (crc8(b) != b[2] || crc8(b + 3) != b[5]) return;   // 偶发 CRC 错就丢掉这一帧
      t = -45 + 175.0f * ((b[0] << 8) | b[1]) / 65535.0f;
      rh = 100.0f * ((b[3] << 8) | b[4]) / 65535.0f;
      float sinceLast = lastOk ? (now - lastOk) / 1000.0f : 0;
      lastOk = now;
      if (isnan(baseRh)) baseRh = rh;
      else if (rh < baseRh) baseRh += (rh - baseRh) * min(1.0f, sinceLast / 3.0f);     // 3 秒跟下去
      else                  baseRh += (rh - baseRh) * min(1.0f, sinceLast / 60.0f);    // 1 分钟跟上来
    }
#endif
  }
  void fail() { Serial.println("[th] lost, retrying"); addr = 0; pending = false; }
  bool valid() { return addr && !isnan(rh) && millis() - lastOk < 3000; }
  // 摸头程度 0–1
  float pet() {
    if (!valid()) return 0;
    float rise = rh - baseRh;
    return constrain((rise - PET_RISE_START) / (PET_RISE_FULL - PET_RISE_START), 0.0f, 1.0f);
  }
};
// SENSORS_PART2
// ===================== 红外 =====================
struct IrSensor {
  int stable = -1, lastRaw = -1;
  uint32_t lastChange = 0;
  void begin() { pinMode(PIN_IR, INPUT); }
  bool hand() {
#if USE_IR
    int raw = digitalRead(PIN_IR);
    uint32_t now = millis();
    if (raw != lastRaw) { lastRaw = raw; lastChange = now; }
    if (stable < 0 || (raw != stable && now - lastChange >= 80)) stable = raw;
    return stable == IR_HAND_LEVEL;
#else
    return true;                    // 不用红外时，当作手一直在嘴里
#endif
  }
};

// ===================== PDM 麦克风 =====================
// 读 I2S 用一个独立的 FreeRTOS 任务，主循环只读最新的音量。
struct Mic {
  I2SClass i2s{I2S_NUM_0};                  // PDM 只能用 I2S0；I2S1 给喇叭
  volatile float db = -99;          // 最近 50ms 音量（相对满量程）
  volatile float level = -99;       // 平滑后的音量，用来算吵闹
  volatile float pkMax = -99;       // 上次读取后的最大原始音量（网页看拍手峰值用）
  volatile float floorDb = NAN;     // 环境底噪：往下快，往上慢
  volatile bool alive = false;      // 数据有波动 = 麦克风真的在工作
  volatile uint32_t lastFrame = 0;
  bool started = false;

  static void task(void *arg) {
    Mic *m = (Mic *)arg;
    static int16_t buf[800];        // 16kHz × 50ms
    for (;;) {
      size_t n = m->i2s.readBytes((char *)buf, sizeof(buf)) / 2;
      if (n < 100) { vTaskDelay(20); continue; }
      int64_t s = 0; int16_t mn = 32767, mx = -32768;
      for (size_t i = 0; i < n; i++) { s += buf[i]; mn = min(mn, buf[i]); mx = max(mx, buf[i]); }
      int32_t mean = s / (int64_t)n;
      double sq = 0;
      for (size_t i = 0; i < n; i++) { double v = buf[i] - mean; sq += v * v; }
      double rms = sqrt(sq / n);
      m->alive = (mx - mn) > 4;
      float d = rms > 0.5 ? 20 * log10(rms / 32768.0) : -99;
      m->db = d;
      if (d > m->pkMax) m->pkMax = d;
      if (m->alive) m->level = m->level < -98 ? d : m->level + (d - m->level) * MIC_SMOOTH;
      if (!MIC_ADAPTIVE) m->floorDb = MIC_QUIET_DB;
      else if (m->alive) {
        float lv = m->level;
        if (isnan(m->floorDb)) m->floorDb = MIC_QUIET_DB;
        else if (lv < m->floorDb) m->floorDb += (lv - m->floorDb) * 0.02f;
        else                      m->floorDb += (lv - m->floorDb) * 0.0004f;
        if (m->floorDb < MIC_QUIET_DB) m->floorDb = MIC_QUIET_DB;
      }
      m->lastFrame = millis();
    }
  }
  void begin() {
#if USE_MIC
    i2s.setPinsPdmRx(PIN_MIC_CLK, PIN_MIC_DATA);
    started = i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    Serial.printf("[mic] PDM begin %s\n", started ? "ok" : "FAILED");
    if (started) xTaskCreatePinnedToCore(task, "mic", 4096, this, 1, nullptr, 0);
#endif
  }
  bool valid() { return started && alive && !isnan(floorDb) && millis() - lastFrame < 500; }
  float loud() { return valid() ? max(0.0f, level - floorDb) : 0; }
  float takePeak() { float p = pkMax; pkMax = -99; return valid() && p > -98 ? max(0.0f, p - floorDb) : 0; }
  float peak() { return valid() ? max(0.0f, db - floorDb) : 0; }
};

