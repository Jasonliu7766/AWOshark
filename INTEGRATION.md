# 音效模块接入说明（sfx.h / sfx.cpp）

程序合成，无需音频素材；非阻塞（后台任务播放，不卡游戏循环）。

## 1. 放文件
把 `sfx.h`、`sfx.cpp` 放进小鲨的 Arduino sketch 目录（和 `.ino` 同级）。

## 2. 填引脚（必须）
`sfx.h` 顶部三个宏改成功放真实引脚（来自 `config.h` 或探测结果）：
```c
#define SFX_PIN_BCLK  <x>   // 功放 BCLK
#define SFX_PIN_LRC   <y>   // 功放 LRC / WS
#define SFX_PIN_DIN   <z>   // ESP32 DOUT -> 功放 DIN
```
注意：麦克风占用 I2S0（PDM），本模块扬声器会走 I2S1，不冲突。
若原固件已自己初始化扬声器 I2S，二选一：让 sfx 接管（推荐），或删掉 sfx 的
`s_i2s.begin()` 改为复用现有 I2S 写入。

## 3. 在 .ino 里接入
```cpp
#include "sfx.h"

void setup() {
  // ... 原有初始化 ...
  sfxBegin();          // 用 sfx.h 里的引脚宏
  sfxPlay(SFX_BOOT);   // 开机音
}
```

## 4. 五个触发点（插到对应事件处）
| 事件 | 在哪触发 | 调用 |
|---|---|---|
| 咬到人（真咬）| `tooth N is the BITE tooth -> real bite` 那段 | `sfxPlay(SFX_SCREAM);` |
| 拍手 | `[mood] startled!` / 检测到 clap 那段 | `sfxPlay(SFX_CLAP);` |
| 开机 | `setup()` 末尾 | `sfxPlay(SFX_BOOT);` |
| 摸头到阈值 | pet 越过 `prs`（开始算摸头）时，加边沿判断只响一次 | `sfxPlay(SFX_PURR);` |
| 怒气满 | anger 首次到 100 时，加边沿判断只响一次 | `sfxPlay(SFX_ANGER);` |

“只响一次”示例（避免每帧重复触发）：
```cpp
static bool angerWasFull = false;
bool full = (anger >= 100.0f);
if (full && !angerWasFull) sfxPlay(SFX_ANGER);
angerWasFull = full;
```

## 5. 和现有 vol 命令对接
处理 `vol<0-100>` 命令时加一行：`sfxSetVolume(v);`

## 6. 想调音色
所有“曲谱”在 `sfx.cpp` 的 `render()` 里，一行一个 `tone(f0,f1,时长ms,波形,颤音Hz,颤音深度,噪声,音量)`。
波形：0 正弦 / 1 方波 / 2 锯齿 / 3 三角。改数字即可换音效，不碰其他逻辑。
