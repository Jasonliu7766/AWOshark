#pragma once
// 输出层：灯板和舵机。
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// ===================== 灯板 =====================
struct Leds {
  Adafruit_NeoPixel strip{LED_W * LED_H, PIN_LED, NEO_GRB + NEO_KHZ800};

  void begin() {
#if USE_LED
    strip.begin();
    strip.setBrightness(LED_BRIGHTNESS);
    strip.clear();
    strip.show();
#endif
  }
  uint16_t idx(int x, int y) {
    if (LED_SERPENTINE && (y & 1)) x = LED_W - 1 - x;
    return y * LED_W + x;
  }
  void px(int x, int y, uint32_t c) {
    if (x >= 0 && x < LED_W && y >= 0 && y < LED_H) strip.setPixelColor(idx(x, y), c);
  }
  // 牙 n（1–16）的 2×2 小块
  void tooth(int n, uint32_t c) {
    int r = (n - 1) / 4, col = (n - 1) % 4;
    if (LED_TRANSPOSE) { int t = r; r = col; col = t; }
    if (LED_FLIP_X) col = 3 - col;
    if (LED_FLIP_Y) r = 3 - r;
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++) px(col * 2 + dx, r * 2 + dy, c);
  }
  void fill(uint32_t c) { strip.fill(c); }
  void clear() { strip.clear(); }
  void show() {
#if USE_LED
    strip.show();
#endif
  }
  static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return Adafruit_NeoPixel::Color(r, g, b); }
  static uint32_t scale(uint32_t c, float k) {
    k = constrain(k, 0.0f, 1.0f);
    return rgb(((c >> 16) & 0xFF) * k, ((c >> 8) & 0xFF) * k, (c & 0xFF) * k);
  }
  // 心情色：怒气 0 = 蓝，30 = 青，50 = 绿，65 = 黄，80 = 橙，100 = 红
  static uint32_t mood(float anger) {
    static const float A[] = {0, 30, 50, 65, 80, 100};
    static const uint8_t C[][3] = {{0, 40, 255}, {0, 200, 200}, {0, 255, 40}, {255, 200, 0}, {255, 80, 0}, {255, 0, 0}};
    anger = constrain(anger, 0.0f, 100.0f);
    int i = 0;
    while (i < 4 && anger > A[i + 1]) i++;
    float f = (anger - A[i]) / (A[i + 1] - A[i]);
    return rgb(C[i][0] + (C[i + 1][0] - C[i][0]) * f, C[i][1] + (C[i + 1][1] - C[i][1]) * f,
               C[i][2] + (C[i + 1][2] - C[i][2]) * f);
  }
  static uint32_t wheel(uint8_t p) {
    p = 255 - p;
    if (p < 85) return rgb(255 - p * 3, 0, p * 3);
    if (p < 170) { p -= 85; return rgb(0, p * 3, 255 - p * 3); }
    p -= 170; return rgb(p * 3, 255 - p * 3, 0);
  }
};

// ===================== 嘴：TB6612 驱动有刷电机，按时长转 + 机械挡块 =====================
struct Jaw {
  enum Phase { IDLE, CLOSING, HOLD, OPENING, TEASE_OUT, TEASE_BACK };
  Phase phase = IDLE;
  uint32_t until = 0;
  bool isOpen = true;                       // 上电时认为嘴是张开的
  uint32_t holdAt = 0;                      // 合嘴停住的时刻

  void begin() {
#if USE_JAW
    pinMode(PIN_MOTOR_IN1, OUTPUT);
    pinMode(PIN_MOTOR_IN2, OUTPUT);
    ledcAttach(PIN_MOTOR_PWM, 20000, 10);
    brake();
#endif
  }
  void drive(int pct) {                     // -100..100，正 = 合嘴方向
#if USE_JAW
    pct = constrain(pct, -100, 100);
    if (JAW_REVERSE) pct = -pct;
    if (pct == 0) { brake(); return; }
    digitalWrite(PIN_MOTOR_IN1, pct > 0);
    digitalWrite(PIN_MOTOR_IN2, pct < 0);
    ledcWrite(PIN_MOTOR_PWM, (uint32_t)abs(pct) * 1023 / 100);
#endif
  }
  void brake() {
#if USE_JAW
    digitalWrite(PIN_MOTOR_IN1, HIGH);
    digitalWrite(PIN_MOTOR_IN2, HIGH);
    ledcWrite(PIN_MOTOR_PWM, 0);
#endif
  }
  void start(Phase p, int pct, uint32_t ms) { phase = p; drive(pct); until = millis() + ms; }

  void bite() {
    if (!isOpen && phase != OPENING) return;
    isOpen = false;
    start(CLOSING, JAW_SPEED_PCT, JAW_CLOSE_MS);
  }
  void open() {
    if (isOpen && phase == IDLE) return;
    if (phase == TEASE_OUT || phase == TEASE_BACK) return;
    start(OPENING, -JAW_SPEED_PCT, JAW_OPEN_MS);
  }
  void tease() {
    if (!isOpen || phase != IDLE) return;
    start(TEASE_OUT, JAW_SPEED_PCT, JAW_CLOSE_MS * JAW_FAKE_RATIO);
  }
  void update() {
    if (phase == IDLE || phase == HOLD || millis() < until) return;
    switch (phase) {
      case CLOSING:    brake(); phase = HOLD; holdAt = millis(); break;
      case OPENING:    brake(); phase = IDLE; isOpen = true; break;
      case TEASE_OUT:  start(TEASE_BACK, -JAW_SPEED_PCT, JAW_OPEN_MS); break;
      case TEASE_BACK: brake(); phase = IDLE; break;
      default: break;
    }
  }
  const char *name() {
    static const char *N[] = {"IDLE", "CLOSING", "HOLD", "OPENING", "TEASE_OUT", "TEASE_BACK"};
    return N[phase];
  }
};
