// 暴躁小鲨 主程序 v1 —— 玩法见 GAME.md，参数见 config.h
// （音效集成版：USE_SPK=1 + sound.h 5 个音效 + 5 个触发点）
#include <Wire.h>
#include "config.h"
#include "sensors.h"
#include "outputs.h"
#include "sound.h"
#include <Preferences.h>
#include "params.h"

Keypad keypad;
TempHum th;
IrSensor ir;
Mic mic;
Leds leds;
Jaw jaw;
Speaker spk;

// ---------- 游戏状态 ----------
enum State { INTRO, PLAY, SUSPENSE, SAFE, BITE, WIN, OVER, LEDTEST, REST, FAKE };
const char *STATE_NAME[] = {"INTRO", "PLAY", "SUSPENSE", "SAFE", "BITE", "WIN", "OVER", "LEDTEST", "REST", "FAKE"};
uint32_t restMs = 0;
uint32_t biteAt = 0;     // 真咬开始的时刻，灯光按它和电机动作同步
#define BITE_FLASH_MS 1000   // 真咬时整块红色快闪多久
bool jawTest = false;           // 排查：电机来回摆，jtest on / off
int calMs = 0;                  // 标定：累计合嘴时长
bool roarLoop = false;           // 调试：每 2 秒叫一次
int busyTooth = 0; uint32_t busyUntil = 0;   // 忙时按下的牙：闪一下暗光提示"等一下"
State state = INTRO;
uint32_t stateAt = 0;

float anger = ANGER_REST;
bool pulled[17];
int bomb = 0;
int curTooth = 0;
uint32_t suspenseMs = 0;
float lastProb = 0;
int bites = 0, rounds = 0, fakes = 0;
uint32_t lastTease = 0, flashUntil = 0;
uint32_t ledTestAt = 0; int ledTestN = 0;

// 串口模拟
int handOverride = -1;          // -1 = 用红外，0/1 = 强制
uint32_t fakePetUntil = 0, fakeNoiseUntil = 0;

void go(State s) {
  state = s; stateAt = millis();
  Serial.printf("[game] -> %s  anger=%.0f\n", STATE_NAME[s], anger);
}
uint32_t inState() { return millis() - stateAt; }

bool handIn() { return handOverride >= 0 ? handOverride : ir.hand(); }
float petLevel() { return millis() < fakePetUntil ? 1.0f : th.pet(); }
float loudDb() { return millis() < fakeNoiseUntil ? 20.0f : mic.loud(); }

// 怒气 -> 按到安全牙时假咬的概率
float fakeProb() { return constrain((anger - FAKE_ANGER_MIN) / max(1.0f, FAKE_ANGER_FULL - FAKE_ANGER_MIN), 0.0f, 1.0f); }
int remaining() { int r = 0; for (int i = 1; i <= 16; i++) r += !pulled[i]; return r; }

void newRound() {
  rounds++;
  for (int i = 1; i <= 16; i++) pulled[i] = false;
  bomb = 1 + esp_random() % 16;
  curTooth = 0;
  jaw.open();
  Serial.printf("[game] round %d start (bites so far %d)\n", rounds, bites);
  go(INTRO);
}
// INO_PART2
// ---------- 怒气更新 ----------
bool angerLocked = false;       // 调试：锁定怒气
uint8_t logMask = 0xFF;         // 调试：每秒输出哪些部分

void updateAnger(float dt) {
  if (angerLocked) return;
  float rest = min(100.0f, ANGER_REST + ANGER_PER_TOOTH * (16 - remaining()));
  anger += (rest - anger) * min(1.0f, ANGER_RELAX_PER_S * dt / 35.0f);
  anger -= PET_CALM_PER_S * petLevel() * dt;
  bool warm = millis() < MIC_WARMUP_MS;
  float l = warm ? 0.0f : loudDb();
  float pk = warm ? 0.0f : (millis() < fakeNoiseUntil ? 20.0f : mic.peak());
  anger += NOISE_ANGER_MAX_S * constrain((l - NOISE_START_DB) / max(0.5f, NOISE_FULL_DB - NOISE_START_DB), 0.0f, 1.0f) * dt;
  static bool clapArmed = true;
  static uint32_t lastClap = 0;
  if (pk > CLAP_DB && clapArmed && millis() - lastClap > (uint32_t)CLAP_COOLDOWN_MS) {
    lastClap = millis();
    clapArmed = false;
    anger += CLAP_ANGER;
    flashUntil = millis() + 120;
    Serial.printf("[mood] startled! +%.0f  (peak=%.0fdB)\n", CLAP_ANGER, pk);
    spk.play(SFX_CLAP);                                   // 新增：拍手回应
  } else if (pk < CLAP_DB - 8) clapArmed = true;
  anger = constrain(anger, 0.0f, 100.0f);
  static bool angerWasFull = false;                       // 新增：怒气满警告（上升沿只响一次）
  bool full = anger >= 99.0f;
  if (full && !angerWasFull) spk.play(SFX_ANGER);
  angerWasFull = full;
  static bool petWas = false;                             // 新增：摸头呼噜（越过阈值响一次）
  bool petNow = petLevel() > 0.3f;
  if (petNow && !petWas) spk.play(SFX_PURR);
  petWas = petNow;
}

// ---------- 灯光 ----------
void drawMood(float dim = 1.0f) {
  uint32_t t = millis();
  float hz = 0.25f + anger / 100.0f * 1.5f;
  float breathe = 0.55f + 0.45f * sinf(t / 1000.0f * hz * 2 * PI);
  if (anger > 85) breathe = ((t / 120) & 1) ? 1.0f : 0.25f;
  uint32_t c = Leds::scale(Leds::mood(anger), breathe * dim);
  leds.clear();
  for (int i = 1; i <= 16; i++) if (!pulled[i]) leds.tooth(i, c);
}
// INO_PART3
void render() {
  uint32_t t = millis();
  switch (state) {
    case INTRO: {
      leds.clear();
      int n = min(16, (int)(inState() / 70) + 1);
      for (int i = 1; i <= n; i++) leds.tooth(i, Leds::mood(anger));
      break;
    }
    case PLAY:
      drawMood();
      break;
    case SUSPENSE: {
      drawMood(0.15f);
      float k = (float)inState() / suspenseMs;
      int period = max(40, (int)(220 - 180 * k));
      leds.tooth(curTooth, ((t / period) & 1) ? Leds::rgb(255, 0, 0) : Leds::rgb(255, 255, 255));
      break;
    }
    case SAFE:
      drawMood(0.4f);
      leds.tooth(curTooth, Leds::scale(Leds::rgb(0, 255, 0), 1.0f - inState() / 500.0f));
      break;
    case BITE: case REST: case OVER: {
      uint32_t e = t - biteAt;
      if (e < BITE_FLASH_MS) leds.fill(((t / 80) & 1) ? Leds::rgb(255, 0, 0) : 0);
      else if (jaw.phase == Jaw::OPENING) {
        float b = 0.2f + 0.2f * sinf(t / 1000.0f * 1.0f * 2 * PI);
        leds.fill(Leds::scale(Leds::rgb(255, 0, 0), b));
      } else leds.fill(Leds::scale(Leds::rgb(255, 0, 0), 0.15f));
      break;
    }
    case FAKE:
      drawMood(0.3f);
      leds.tooth(curTooth, ((t / 60) & 1) ? Leds::rgb(255, 60, 0) : 0);
      break;
    case WIN:
      for (int y = 0; y < LED_H; y++)
        for (int x = 0; x < LED_W; x++) leds.px(x, y, Leds::wheel((x * 16 + y * 16 + t / 8) & 255));
      break;
    case LEDTEST:
      leds.clear();
      leds.tooth(ledTestN, Leds::rgb(255, 255, 255));
      break;
  }
  if (t < busyUntil && busyTooth && !pulled[busyTooth] && busyTooth != curTooth &&
      (state == INTRO || state == SUSPENSE || state == SAFE))
    leds.tooth(busyTooth, Leds::rgb(60, 60, 60));
  if (t < flashUntil) leds.fill(Leds::rgb(180, 180, 180));
  leds.show();
}

// ---------- 按牙 ----------
void onTooth(int n) {
  Serial.printf("[key] tooth %d  hand=%d  state=%s\n", n, handIn(), STATE_NAME[state]);
  if (state != PLAY) {
    Serial.printf("[game] ignored: busy (%s), wait for PLAY\n", STATE_NAME[state]);
    busyTooth = n; busyUntil = millis() + 150;
    return;
  }
  if (pulled[n]) { Serial.println("[game] already pulled"); return; }
  if (!handIn()) {
    Serial.println("[game] no hand in mouth, ignored");
    flashUntil = millis() + 80;
    return;
  }
  curTooth = n;
  uint32_t smin = SUSPENSE_MIN_MS, smax = max(SUSPENSE_MIN_MS, SUSPENSE_MAX_MS);
  suspenseMs = smin + esp_random() % (smax - smin + 1);
  go(SUSPENSE);
}
// INO_PART4
// ---------- 状态机 ----------
void step() {
  switch (state) {
    case INTRO:
      if (inState() > 16 * 70 + 400) go(PLAY);
      break;

    case PLAY:
      if (!angerLocked && anger >= TEASE_ANGER && !handIn() && millis() - lastTease > TEASE_COOLDOWN_MS) {
        lastTease = millis();
        Serial.println("[game] tease snap");
        jaw.tease();
      }
      break;

    case SUSPENSE:
      if (inState() >= suspenseMs) {
        if (curTooth == bomb) {
          Serial.printf("[game] tooth %d is the BITE tooth -> real bite  anger=%.0f\n", curTooth, anger);
          bites++;
          jaw.bite();
          biteAt = millis();
          spk.play(SFX_SCREAM);                 // 改：咬到人 -> 惨叫（原来是 spk.roar()）
          go(BITE);
        } else {
          lastProb = fakeProb();
          float roll = (esp_random() % 10000) / 10000.0f;
          Serial.printf("[game] tooth %d safe  anger=%.0f  fake=%.0f%%  roll=%.0f%%\n", curTooth, anger,
                        lastProb * 100, roll * 100);
          if (roll < lastProb && jaw.phase == Jaw::IDLE) {
            fakes++;
            jaw.tease();
            go(FAKE);
          } else {
            pulled[curTooth] = true;
            if (!angerLocked) anger = min(100.0f, anger + ANGER_PER_PULL);
            go(SAFE);
          }
        }
      }
      break;
    // INO_PART4B
    case FAKE:
      if (inState() > 600 && jaw.phase != Jaw::TEASE_OUT) {
        pulled[curTooth] = true;
        if (!angerLocked) anger = min(100.0f, anger + ANGER_PER_PULL);
        go(SAFE);
      }
      break;

    case SAFE:
      if (inState() > 300) {
        if (remaining() == 0) go(WIN);
        else go(PLAY);
      }
      break;

    case BITE:
      if (jaw.phase == Jaw::HOLD) {
        if (!angerLocked) anger = ANGER_AFTER_BITE;
        restMs = (uint32_t)JAW_REST_MS;
        Serial.printf("[game] bitten, mouth closed for %lums\n", restMs);
        go(REST);
      }
      break;

    case REST:
      if (inState() >= restMs) {
        Serial.println("[game] shark opens its mouth");
        jaw.open();
        go(OVER);
      }
      break;

    case WIN:
      if (inState() > 4000) newRound();
      break;

    case OVER:
      if (inState() > ROUND_RESTART_MS && jaw.phase == Jaw::IDLE) newRound();
      break;

    case LEDTEST:
      if (millis() - ledTestAt > 600) {
        ledTestAt = millis();
        if (++ledTestN > 16) { ledTestN = 0; go(PLAY); }
        else Serial.printf("[led] tooth %d\n", ledTestN);
      }
      break;
  }
}
// INO_PART5
// ---------- 网页调参：参数表、保存、实时数据 ----------
Param PARAMS[] = {
  {"jsp", "JAW_SPEED_PCT",  &JAW_SPEED_PCT,  DEF_JAW_SPEED_PCT,  10, 100, 5,   "电机", "真咬速度 %"},
  {"jcm", "JAW_CLOSE_MS",   &JAW_CLOSE_MS,   DEF_JAW_CLOSE_MS,   10, 1000, 5,  "电机", "咬合转动时长 ms"},
  {"jom", "JAW_OPEN_MS",    &JAW_OPEN_MS,    DEF_JAW_OPEN_MS,    10, 20000, 100, "电机", "抬头转动时长 ms"},
  {"mqd", "MIC_QUIET_DB",   &MIC_QUIET_DB,   DEF_MIC_QUIET_DB,  -80, -20, 0.5, "麦克风", "安静基线 dB"},
  {"nsd", "NOISE_START_DB", &NOISE_START_DB, DEF_NOISE_START_DB,  0, 40, 0.5,  "麦克风", "开始生气 +dB"},
  {"nfd", "NOISE_FULL_DB",  &NOISE_FULL_DB,  DEF_NOISE_FULL_DB,   1, 50, 0.5,  "麦克风", "生气最快 +dB"},
  {"cld", "CLAP_DB",        &CLAP_DB,        DEF_CLAP_DB,         5, 60, 0.5,  "麦克风", "拍手阈值 +dB"},
  {"prs", "PET_RISE_START", &PET_RISE_START, DEF_PET_RISE_START,  0, 40, 0.5,  "湿度", "开始算摸头 +%RH"},
  {"prf", "PET_RISE_FULL",  &PET_RISE_FULL,  DEF_PET_RISE_FULL,   1, 60, 0.5,  "湿度", "摸满 +%RH"},
  {"fra", "JAW_FAKE_RATIO", &JAW_FAKE_RATIO, DEF_JAW_FAKE_RATIO, 0.1, 1, 0.05, "电机", "假咬幅度（真咬的几倍）"},
  {"fmn", "FAKE_ANGER_MIN",    &FAKE_ANGER_MIN,    DEF_FAKE_ANGER_MIN,    0, 100, 1,  "怒气", "超过这个怒气开始可能假咬"},
  {"fmx", "FAKE_ANGER_FULL",   &FAKE_ANGER_FULL,   DEF_FAKE_ANGER_FULL,   1, 100, 1,  "怒气", "到这个怒气必假咬"},
  {"nam", "NOISE_ANGER_MAX_S", &NOISE_ANGER_MAX_S, DEF_NOISE_ANGER_MAX_S, 0, 200, 1,  "怒气", "吵闹时每秒最多 +"},
  {"cla", "CLAP_ANGER",        &CLAP_ANGER,        DEF_CLAP_ANGER,        0, 100, 1,  "怒气", "拍手一次 +"},
  {"ccd", "CLAP_COOLDOWN_MS",  &CLAP_COOLDOWN_MS,  DEF_CLAP_COOLDOWN_MS,  0, 10000, 100, "怒气", "两次拍手至少隔 ms"},
  {"msm", "MIC_SMOOTH",        &MIC_SMOOTH,        DEF_MIC_SMOOTH,        0.02, 1, 0.01, "麦克风", "音量反应速度（1=不平滑）"},
  {"app", "ANGER_PER_PULL",    &ANGER_PER_PULL,    DEF_ANGER_PER_PULL,    0, 50, 1,   "怒气", "拔一颗牙 +"},
  {"apt", "ANGER_PER_TOOTH",   &ANGER_PER_TOOTH,   DEF_ANGER_PER_TOOTH,   0, 10, 0.5, "怒气", "每少一颗牙，平时怒气 +"},
  {"jrs", "JAW_REST_MS",       &JAW_REST_MS,       DEF_JAW_REST_MS,       0, 15000, 100, "电机", "咬住后多久自动抬头 ms"},
  {"smn", "SUSPENSE_MIN_MS",   &SUSPENSE_MIN_MS,   DEF_SUSPENSE_MIN_MS,   0, 3000, 50, "怒气", "按牙后悬念最短 ms"},
  {"smx", "SUSPENSE_MAX_MS",   &SUSPENSE_MAX_MS,   DEF_SUSPENSE_MAX_MS,   0, 3000, 50, "怒气", "按牙后悬念最长 ms"},
};
const int NPARAMS = sizeof(PARAMS) / sizeof(PARAMS[0]);
Preferences prefs;
bool tele = false;

void printParam(const Param &p) {
  Serial.printf("[P] {\"k\":\"%s\",\"n\":\"%s\",\"v\":%g,\"d\":%g,\"min\":%g,\"max\":%g,\"st\":%g,\"g\":\"%s\",\"l\":\"%s\"}\n",
                p.key, p.name, *p.v, p.def, p.mn, p.mx, p.step, p.group, p.label);
}
// INO_PART5B
void loadParams() {
  prefs.begin("shark", true);
  for (auto &p : PARAMS) *p.v = prefs.getFloat(p.key, p.def);
  prefs.end();
}
void saveParams() {
  prefs.begin("shark", false);
  for (auto &p : PARAMS) prefs.putFloat(p.key, *p.v);
  prefs.end();
  Serial.println("[saved] 参数已保存到闪存");
}
void defaultParams() {
  prefs.begin("shark", false); prefs.clear(); prefs.end();
  for (auto &p : PARAMS) { *p.v = p.def; printParam(p); }
  Serial.println("[saved] 已恢复 config.h 默认值");
}
bool setParam(String key, float v) {
  for (auto &p : PARAMS)
    if (key == p.key || key == p.name) { *p.v = constrain(v, p.mn, p.mx); printParam(p); return true; }
  return false;
}
void sendTele() {
  Serial.printf("[T] {\"st\":\"%s\",\"an\":%.1f,\"lk\":%d,\"fk\":%.2f,\"db\":%.1f,\"lv\":%.1f,\"fl\":%.1f,\"ld\":%.1f,\"pk\":%.1f,"
                "\"T\":%.1f,\"rh\":%.1f,\"br\":%.1f,\"pet\":%.2f,\"hand\":%d,\"key\":%d,\"th\":%d,\"mic\":%d,\"jaw\":\"%s\",\"open\":%d,\"rem\":%d,\"cal\":%d}\n",
                STATE_NAME[state], anger, angerLocked, fakeProb(), mic.db, mic.level, mic.floorDb, mic.loud(), mic.takePeak(),
                th.t, th.rh, th.baseRh, petLevel(), handIn(), keypad.ok, th.valid(), mic.valid(), jaw.name(), jaw.isOpen, remaining(), calMs);
}

// ---------- 串口命令 ----------
void help() {
  Serial.println("cmds: k<1-16> | h1 h0 ha | pet | noise | a<0-100> | lock unlock | r | jc jo jms<ms> jom<ms> jf<ms> jb<ms> j<-100..100> | led | roar | roar on/off | vol<0-100> | s | ?");
  Serial.println("      t1 红外 | t2 按键 | t3 灯色 | t4 摸头 | t5 噪声 | t6 咬人 | t7 假咬 | t0 结束调试");
  Serial.println("      log all | log ir | log th | log mic | log off");
}
// INO_PART6
// 单项调试：设置好环境并只打印相关信息
void testMode(int n) {
  static const char *NAME[] = {"结束调试", "红外", "按键/拔牙", "灯色", "摸头消气", "噪声生气", "咬人", "假咬"};
  if (n < 0 || n > 7) return;
  Serial.printf("\n[test] ===== %d %s =====\n", n, NAME[n]);
  switch (n) {
    case 0: angerLocked = false; logMask = 0xFF; handOverride = -1;
            Serial.println("[test] 怒气解锁，恢复正常游戏"); break;
    case 1: angerLocked = true; anger = 20; logMask = 1;
            Serial.println("[test] 只看 hand=：手伸进嘴里应为 1，拿出应为 0"); break;
    case 2: angerLocked = true; anger = 0; handOverride = 1; logMask = 0; newRound();
            Serial.printf("[test] 怒气锁 0，不用红外。炸弹牙是 %d 号，最后再按它\n", bomb);
            Serial.println("[test] 逐颗按 1–16：每颗应闪绿后熄灭，等它熄灭再按下一颗"); break;
    case 3: angerLocked = true; logMask = 0;
            Serial.println("[test] 用 a0 a30 a50 a65 a80 a100 看颜色：蓝 青 绿 黄 橙 红（锁定，不会漂）"); break;
    case 4: angerLocked = false; anger = 80; logMask = 2;
            Serial.println("[test] 怒气 80 起步。手捂住温湿度传感器 10 秒：RH 升、pet 升、anger 降、灯变蓝"); break;
    case 5: angerLocked = false; anger = 30; logMask = 4;
            Serial.println("[test] 怒气 30 起步。大声说话 / 拍手：loud 升、anger 升、灯变红；拍手打印 startled"); break;
    case 7: angerLocked = true; anger = 100; handOverride = 1; logMask = 0; newRound();
            Serial.printf("[test] 怒气锁 100（必假咬），不用红外。按除 %d 号以外的牙：每颗都应合约 10° 再张开，然后闪绿熄灭\n", bomb); break;
    case 6: angerLocked = true; anger = 20; handOverride = -1; logMask = 1; newRound();
            Serial.printf("[test] 怒气锁 20（不假咬）。手伸进嘴里按 %d 号（咬人牙，必咬 30°）。抽出手：合约 30° 后停住，10 秒后自己张嘴\n", bomb); break;
  }
}
void status() {
  Serial.printf("[status] state=%s anger=%.1f fake=%.0f%% bomb=%d remaining=%d bites=%d fakes=%d rounds=%d\n",
                STATE_NAME[state], anger, fakeProb() * 100, bomb, remaining(), bites, fakes, rounds);
  Serial.printf("[status] hand=%d(ir=%d ovr=%d) key=%s th=%s T=%.1f RH=%.1f base=%.1f pet=%.2f\n",
                handIn(), ir.hand(), handOverride, keypad.ok ? "ok" : "--", th.valid() ? "ok" : "--",
                th.t, th.rh, th.baseRh, petLevel());
  Serial.printf("[status] mic=%s db=%.1f floor=%.1f loud=%.1f jaw=%s open=%d closeMs=%lu\n",
                mic.valid() ? "ok" : "--", mic.db, mic.floorDb, loudDb(), jaw.name(), jaw.isOpen, (uint32_t)JAW_CLOSE_MS);
  Serial.print("[status] teeth:");
  for (int i = 1; i <= 16; i++) Serial.printf(" %d%s", i, pulled[i] ? "x" : (i == bomb ? "*" : ""));
  Serial.println("   (x=已拔 *=咬人牙)");
}
// INO_PART6B
void command(String c) {
  c.trim();
  if (!c.length()) return;
  if (c[0] == 'k' && isDigit(c[1])) { int n = c.substring(1).toInt(); if (n >= 1 && n <= 16) onTooth(n); }
  else if (c == "h1") { handOverride = 1; Serial.println("[sim] hand IN"); }
  else if (c == "h0") { handOverride = 0; Serial.println("[sim] hand OUT"); }
  else if (c == "ha") { handOverride = -1; Serial.println("[sim] hand = IR sensor"); }
  else if (c == "pet") { fakePetUntil = millis() + 5000; Serial.println("[sim] petting 5s"); }
  else if (c == "noise") { fakeNoiseUntil = millis() + 3000; Serial.println("[sim] noise 3s"); }
  else if (c[0] == 'a' && isDigit(c[1])) { anger = constrain(c.substring(1).toFloat(), 0.0f, 100.0f); Serial.printf("[sim] anger=%.0f\n", anger); }
  else if (c == "r") newRound();
  else if (c == "jc") { jaw.isOpen = true; jaw.phase = Jaw::IDLE; jaw.bite(); Serial.printf("[jaw] close %.0fms @%.0f%%\n", JAW_CLOSE_MS, JAW_SPEED_PCT); }
  else if (c == "jo") { jaw.isOpen = false; jaw.phase = Jaw::HOLD; jaw.open(); Serial.printf("[jaw] open %.0fms @%.0f%%\n", JAW_OPEN_MS, JAW_SPEED_PCT); }
  else if (c.startsWith("jom")) setParam("jom", c.substring(3).toFloat());
  else if (c.startsWith("jms")) { setParam("jcm", c.substring(3).toFloat()); setParam("jom", c.substring(3).toFloat()); }
  else if ((c.startsWith("jf") || c.startsWith("jb")) && isDigit(c[2])) {
    int ms = constrain(c.substring(2).toInt(), 5, 500);
    bool fwd = c[1] == 'f';
    jaw.phase = Jaw::IDLE; jaw.drive(fwd ? JAW_SPEED_PCT : -JAW_SPEED_PCT); delay(ms); jaw.brake();
    calMs += fwd ? ms : -ms;
    Serial.printf("[cal] %s %dms  -> 累计合嘴 %dms（从开始标定时的位置算）\n", fwd ? "合" : "张", ms, calMs);
  }
  else if (c.startsWith("jrun")) {
    int v = c.length() > 4 ? constrain(c.substring(4).toInt(), -100, 100) : 50;
    jawTest = false;
    jaw.phase = Jaw::IDLE; jaw.drive(v);
    Serial.printf("[jrun] 一直转 %d%%，jstop 停止\n", v);
  }
  else if (c == "jstop") { jawTest = false; jaw.phase = Jaw::IDLE; jaw.brake(); Serial.println("[jrun] 停止（刹车）"); }
  else if (c == "jtest on")  { jawTest = true; Serial.println("[jtest] 电机来回摆：50% 正转 200ms，停 0.6s，反转 200ms，停 0.6s。jtest off 停止"); }
  else if (c == "jtest off") { jawTest = false; jaw.phase = Jaw::IDLE; jaw.brake(); Serial.println("[jtest] off"); }
  // INO_PART6C
  else if (c == "jt") { jaw.isOpen = true; jaw.phase = Jaw::IDLE; jaw.tease(); Serial.println("[jaw] fake bite"); }
  else if (c == "params") { for (auto &p : PARAMS) printParam(p); Serial.println("[P] end"); }
  else if (c.startsWith("set ")) { int sp = c.indexOf(' ', 4); if (sp < 0 || !setParam(c.substring(4, sp), c.substring(sp + 1).toFloat())) Serial.println("[P] unknown param"); }
  else if (c == "save") saveParams();
  else if (c == "defaults") defaultParams();
  else if (c == "tele on")  tele = true;
  else if (c == "tele off") tele = false;
  else if (c == "jzero") { calMs = 0; Serial.println("[cal] 当前位置记为 0（张开）"); }
  else if (c[0] == 'j' && (isDigit(c[1]) || c[1] == '-')) { int v = c.substring(1).toInt(); jaw.phase = Jaw::IDLE; jaw.drive(v); delay(150); jaw.brake(); Serial.printf("[jaw] nudge %d%% 150ms\n", v); }
  else if (c == "roar") { spk.roar(); Serial.println("[spk] roar"); }
  else if (c == "roar on")  { roarLoop = true;  Serial.println("[spk] roar every 2s, 'roar off' to stop"); }
  else if (c == "roar off") { roarLoop = false; Serial.println("[spk] roar loop off"); }
  else if (c.startsWith("vol")) { spkVolume = constrain(c.substring(3).toInt(), 0, 100); Serial.printf("[spk] volume %d\n", spkVolume); }
  else if (c == "led") { ledTestN = 0; ledTestAt = 0; go(LEDTEST); }
  else if (c == "s") status();
  else if (c == "lock")   { angerLocked = true;  Serial.printf("[sim] anger LOCKED at %.0f\n", anger); }
  else if (c == "unlock") { angerLocked = false; Serial.println("[sim] anger unlocked"); }
  else if (c[0] == 't' && c.length() == 2 && isDigit(c[1])) testMode(c[1] - '0');
  else if (c == "log all") logMask = 0xFF;
  else if (c == "log ir")  logMask = 1;
  else if (c == "log th")  logMask = 2;
  else if (c == "log mic") logMask = 4;
  else if (c == "log off") logMask = 0;
  else if (c == "log micraw") logMask = 8;
  else if (c == "sfx scream") spk.play(SFX_SCREAM);     // 新增：手动试听各音效
  else if (c == "sfx roar")   spk.play(SFX_ROAR);
  else if (c == "sfx clap")   spk.play(SFX_CLAP);
  else if (c == "sfx purr")   spk.play(SFX_PURR);
  else if (c == "sfx anger")  spk.play(SFX_ANGER);
  else if (c == "sfx boot")   spk.play(SFX_BOOT);
  else help();
}
// INO_PART7
// ---------- 主程序 ----------
void setup() {
  jaw.begin();                 // 最先锁住电机引脚（刹车），避免开机等待期间电机乱转
  loadParams();                // 网页保存过的参数
  Serial.setTxTimeoutMs(1);    // 没开串口监视器时最多等 1ms 就丢弃打印，不卡游戏
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n===== 暴躁小鲨 v1 =====");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  Wire.setTimeOut(30);
  ir.begin();
  leds.begin();
  mic.begin();
  spk.begin();
  spk.play(SFX_BOOT);          // 新增：开机音
  help();
  newRound();
}

void loop() {
  static uint32_t last = millis(), lastLog = 0, lastSensor = 0;
  uint32_t now = millis();
  float dt = (now - last) / 1000.0f;
  last = now;

  if (now - lastSensor >= 20) {                 // 50Hz 读传感器
    lastSensor = now;
    if (int k = keypad.poll()) onTooth(k);
    th.update(dt);
  }
  updateAnger(dt);
  jaw.update();
  static int lastJawPhase = -1;
  static uint32_t jawPhaseAt = 0;
  if (jaw.phase != lastJawPhase) {
    if (lastJawPhase >= 0) Serial.printf("[jaw] %lu %s (上一段 %lums)\n", now, jaw.name(), now - jawPhaseAt);
    lastJawPhase = jaw.phase; jawPhaseAt = now;
  }
  if (jaw.phase == Jaw::HOLD && state != BITE && state != REST && now - jaw.holdAt >= (uint32_t)JAW_REST_MS)
    jaw.open();                                 // 手动"咬一次"之后也按同样时间自动抬头
  step();
  render();

  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { command(line); line = ""; }
    else line += ch;
  }
  // INO_PART7B
  if (jawTest) {                                // 排查用：正反来回摆，净转动为 0
    static int lastSeg = -1;
    int seg = (now % 1600) / 200;                // 0 正转，4 反转，其余刹车
    if (seg != lastSeg) {
      lastSeg = seg;
      jaw.phase = Jaw::IDLE;
      if (seg == 0) { jaw.drive(50);  Serial.println("[jtest] 正转"); }
      else if (seg == 4) { jaw.drive(-50); Serial.println("[jtest] 反转"); }
      else jaw.brake();
    }
  }
  static uint32_t lastTele = 0;
  if (tele && now - lastTele >= 100) { lastTele = now; sendTele(); }   // 网页实时数据，10Hz
  static uint32_t lastRoar = 0;
  if (roarLoop && now - lastRoar >= 2000) { lastRoar = now; spk.roar(); Serial.printf("[spk] roar  vol=%d\n", spkVolume); }
  static uint32_t lastRaw = 0;
  if (logMask == 8 && now - lastRaw >= 50) { lastRaw = now; Serial.printf("[raw] %.1f %.1f\n", mic.db, mic.floorDb); }
  if (now - lastLog >= 1000) {                  // 每秒一行，按 logMask 只打印相关部分
    lastLog = now;
    if (logMask == 0xFF)
      Serial.printf("[mood] anger=%3.0f%s fake=%3.0f%% | RH=%.1f(base %.1f) pet=%.2f | loud=%4.1fdB | hand=%d | %s left=%d\n",
                    anger, angerLocked ? "(L)" : "", fakeProb() * 100, th.rh, th.baseRh, petLevel(), loudDb(), handIn(),
                    STATE_NAME[state], remaining());
    else if (logMask == 1) Serial.printf("[ir] hand=%d  (raw=%d)\n", handIn(), digitalRead(PIN_IR));
    else if (logMask == 2) Serial.printf("[th] RH=%.1f base=%.1f rise=%+.1f pet=%.2f | anger=%.0f\n",
                                         th.rh, th.baseRh, th.rh - th.baseRh, petLevel(), anger);
    else if (logMask == 4) Serial.printf("[mic] db=%.1f floor=%.1f loud=%.1f | anger=%.0f\n",
                                         mic.db, mic.floorDb, loudDb(), anger);
  }
  delay(5);
}








