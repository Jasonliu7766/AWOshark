#!/usr/bin/env bash
# 一键烧录预编译固件到 ESP32-S3(需要 esptool：pip install esptool）
# 用法: ./flash.sh [串口]   例如 ./flash.sh /dev/cu.usbmodem101
set -e
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -z "$PORT" ] && { echo "找不到串口，手动传入：./flash.sh /dev/cu.usbmodemXXXX"; exit 1; }
cd "$(dirname "$0")"
echo "烧录到 $PORT ..."
python3 -m esptool --chip esp32s3 --port "$PORT" --no-stub \
  --before default-reset --after hard-reset write-flash -z \
  0x0     shark.ino.bootloader.bin \
  0x8000  shark.ino.partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 shark.ino.bin
echo "完成。串口 115200 可看日志。"
