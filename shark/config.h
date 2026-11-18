#pragma once
// ===== 暴躁小鲨：所有可调参数集中在这里 =====

// ---------- 引脚（ESP32-S3-N16R8，见 NOTES.md） ----------
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_IR        4      // 红外 DO，高电平 = 有手
#define IR_HAND_LEVEL HIGH
#define PIN_MIC_CLK   5      // PDM 麦克风 SCK
#define PIN_MIC_DATA  6      // PDM 麦克风 SD
#define PIN_LED       13     // WS2812 灯板 DI
#define PIN_MOTOR_PWM 10     // TB6612 PWMA
#define PIN_MOTOR_IN1 11     // TB6612 AIN1
#define PIN_MOTOR_IN2 12     // TB6612 AIN2
#define PIN_SPK_LRC   15     // MAX98357A LRC
#define PIN_SPK_BCLK  16     // MAX98357A BCLK
#define PIN_SPK_DIN   17     // MAX98357A DIN

// ---------- 各模块开关：没接的先关掉，用串口命令模拟 ----------
#define USE_KEYPAD  1
#define USE_TH      1
#define USE_IR      0      // 不用红外：默认手一直在嘴里（用户要求）
#define USE_MIC     1
#define USE_LED     1
#define USE_JAW     1
#define USE_SPK     1      // 喇叭已接好（MAX98357A -> 15/16/17, SD 接 3V3），开启

// ---------- I2C ----------
#define I2C_FREQ_HZ 20000    // 线长或上拉弱时用低速；线短且稳定后可改 100000

// ---------- 灯板 ----------
#define LED_W 8
#define LED_H 8
#define LED_BRIGHTNESS 50    // 0–255，约 20%。灯板必须独立 5V 供电
#define LED_SERPENTINE 0     // 灯珠蛇形走线（每行反向）就设 1
#define LED_FLIP_X     0     // 牙块左右颠倒就设 1
#define LED_FLIP_Y     0     // 牙块上下颠倒就设 1
#define LED_TRANSPOSE  0     // 行列互换就设 1
// 牙 n（1–16）对应灯板上第 (n-1)/4 行、(n-1)%4 列的 2×2 小块；对不上时用上面 4 个开关调

// ---------- 嘴（TB6612 + TT 有刷电机，按时长转）。用串口 "jms<ms>" + "jc" / "jo" 现场调 ----------
#define DEF_JAW_SPEED_PCT    100    // 真咬合嘴 / 张嘴速度（2026-11-15 用户要求最大力度）
#define DEF_JAW_CLOSE_MS     100    // 真咬角度：合嘴转动时长
#define DEF_JAW_OPEN_MS      10000  // 抬头（张嘴）全速反转时长
#define JAW_REVERSE      0      // 合嘴方向反了就设 1
#define DEF_JAW_FAKE_RATIO   0.5    // 假咬 = 真咬速度合 JAW_CLOSE_MS × 这个比例
#define DEF_JAW_REST_MS      0      // 真咬合上后多久抬头

// ---------- 喇叭 ----------
#define SPK_VOLUME       60     // 0–100
#define SPK_ROAR_MS      900    // "嗷"的长度

// ---------- 怒气 ----------
#define ANGER_REST          35.0f
#define ANGER_RELAX_PER_S   1.5f
#define DEF_ANGER_PER_TOOTH 3.0f
#define DEF_ANGER_PER_PULL     12.0f
#define ANGER_AFTER_BITE    20.0f

// 摸头（湿度）
#define DEF_PET_RISE_START   6.0f
#define DEF_PET_RISE_FULL    25.0f
#define PET_CALM_PER_S   12.0f

// 噪声（麦克风，相对安静环境的 dB）
#define DEF_MIC_QUIET_DB     -52.0f
#define MIC_ADAPTIVE     1
#define MIC_WARMUP_MS    4000
#define DEF_NOISE_START_DB   7.0f
#define DEF_NOISE_FULL_DB    13.0f
#define DEF_NOISE_ANGER_MAX_S  60.0f
#define DEF_CLAP_DB          25.0f
#define DEF_CLAP_ANGER         25.0f
#define DEF_CLAP_COOLDOWN_MS 1000
#define DEF_MIC_SMOOTH       0.3

// ---------- 概率 ----------
#define DEF_FAKE_ANGER_MIN     30.0f
#define DEF_FAKE_ANGER_FULL    70.0f

// ---------- 节奏 ----------
#define DEF_SUSPENSE_MIN_MS 300
#define DEF_SUSPENSE_MAX_MS 700
#define ROUND_RESTART_MS 1500
#define TEASE_ANGER      90.0f
#define TEASE_COOLDOWN_MS 10000

// ---------- 网页调参（tuner.html）能在线改的参数 ----------
float JAW_SPEED_PCT  = DEF_JAW_SPEED_PCT;
float JAW_CLOSE_MS   = DEF_JAW_CLOSE_MS;
float JAW_OPEN_MS    = DEF_JAW_OPEN_MS;
float MIC_QUIET_DB   = DEF_MIC_QUIET_DB;
float NOISE_START_DB = DEF_NOISE_START_DB;
float NOISE_FULL_DB  = DEF_NOISE_FULL_DB;
float CLAP_DB        = DEF_CLAP_DB;
float PET_RISE_START = DEF_PET_RISE_START;
float PET_RISE_FULL  = DEF_PET_RISE_FULL;
float JAW_FAKE_RATIO = DEF_JAW_FAKE_RATIO;
float ANGER_PER_PULL    = DEF_ANGER_PER_PULL;
float NOISE_ANGER_MAX_S = DEF_NOISE_ANGER_MAX_S;
float CLAP_ANGER        = DEF_CLAP_ANGER;
float FAKE_ANGER_MIN    = DEF_FAKE_ANGER_MIN;
float FAKE_ANGER_FULL   = DEF_FAKE_ANGER_FULL;
float JAW_REST_MS       = DEF_JAW_REST_MS;
float ANGER_PER_TOOTH   = DEF_ANGER_PER_TOOTH;
float SUSPENSE_MIN_MS   = DEF_SUSPENSE_MIN_MS;
float SUSPENSE_MAX_MS   = DEF_SUSPENSE_MAX_MS;
float CLAP_COOLDOWN_MS  = DEF_CLAP_COOLDOWN_MS;
float MIC_SMOOTH        = DEF_MIC_SMOOTH;
