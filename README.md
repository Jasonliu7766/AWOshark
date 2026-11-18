# AWOshark · 暴躁小鲨

ESP32-S3 的 Arduino "拔牙咬手"互动游戏(暴躁小鲨)+ 程序合成音效。

拔 16 颗牙,其中 1 颗是隐藏"咬人牙",拔到就咬合;靠红外判断手是否在嘴里,温湿度判断"摸头",PDM 麦克风判断噪声/拍手,怒气越高越可能"假咬"。WS2812 灯板显示心情色,TB6612 驱动有刷电机做下颚。

## 硬件 / 引脚(ESP32-S3-N16R8)
| 功能 | 器件 | 引脚 |
|---|---|---|
| 按键 + 牙灯 | TM1650 | I2C SDA=8 / SCL=9 |
| 温湿度(摸头) | GXHT30C @0x44 | 同 I2C |
| 手检测 | 红外 DO | 4(默认关,USE_IR=0) |
| 噪声/拍手 | PDM 麦克风 | CLK=5 / DATA=6 |
| 牙灯 | WS2812 8×8 | 13 |
| 下颚 | TB6612 + 有刷电机 | PWM=10 / IN1=11 / IN2=12 |
| 发声 | MAX98357A I2S 功放 | LRC=15 / BCLK=16 / DIN=17,**SD 接 3V3** |

> 功放 SD 脚务必接 3V3,否则芯片处于关断、没有声音。

## 音效
`sound.h` 的 `Speaker` 里程序现场合成 6 个音效(无需音频文件):
开机音 / 咬人惨叫 / 拍手回应 / 摸头呼噜 / 怒气满警告 / 吼叫。
触发点在 `shark.ino`(开机、咬到人、拍手 startled、怒气≥99、摸头 pet>0.3)。

- `tuner-sfx.js` — 挂到 `tuner.html` 的浏览器调音面板(Web Audio 预览,算法与固件一致)。
- `mp_sound_test.py` — MicroPython 下快速验证功放 / 试听音效。
- `INTEGRATION.md` — 音效接入说明。

## 编译 / 烧录(arduino-cli)
```bash
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  shark
arduino-cli upload -p /dev/cu.usbmodemXXXX \
  --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  shark
```
`CDCOnBoot=cdc` 必须开(否则 `Serial` 走 UART,缺 `setTxTimeoutMs`)。

## 串口命令
`?` 看全部;常用:`s` 状态、`k<1-16>` 拔牙、`roar`、`sfx scream|roar|clap|purr|anger|boot` 试听、`vol<0-100>`、`tele on` 配合调参网页。
