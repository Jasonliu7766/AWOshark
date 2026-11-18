# mp_sound_test.py v2 — 更真实/恐怖的生物音效引擎（流式，低内存）
from machine import I2S, Pin
import math, time, random, gc
from array import array

RATE = 16000
VOL  = 0.85
i2s = I2S(0, sck=Pin(16), ws=Pin(15), sd=Pin(17),
          mode=I2S.TX, bits=16, format=I2S.STEREO, rate=RATE, ibuf=24000)

_N = 1024
_SIN = array('f', [math.sin(2 * math.pi * i / _N) for i in range(_N)])
_CH = 512
_buf = array('h', bytes(_CH * 4))       # 复用的小缓冲：512 帧立体声
_mv = memoryview(_buf)

def play2(f0, f1, ms, parts=((1.0, 1.0),), detune=0.0, growlHz=0.0, growlD=0.0,
          noise=0.0, drive=1.0, amp=0.6, vibHz=0.0, vibD=0.0, pcurve=1.0, atk=0.02, rel=0.3):
    n = RATE * ms // 1000
    np = len(parts)
    ph = [0.0] * np
    gph = 0.0; nz = 0.0; i = 0
    while i < n:
        c = 0
        while c < _CH and i < n:
            t = i / n
            f = f0 + (f1 - f0) * (t ** pcurve)
            if vibD:
                f *= 1.0 + vibD * _SIN[int((vibHz * i / RATE) * _N) & (_N - 1)]
            s = 0.0
            for pi in range(np):
                ratio, lvl = parts[pi]
                ph[pi] += (f * ratio * (1.0 + detune * pi)) / RATE
                s += lvl * _SIN[int(ph[pi] * _N) & (_N - 1)]
            if growlD:
                gph += growlHz / RATE
                s *= (1.0 - growlD) + growlD * (0.6 + 0.4 * _SIN[int(gph * _N) & (_N - 1)]) * 2.0
            if noise:
                nz += 0.3 * ((random.getrandbits(16) / 32768.0 - 1.0) - nz)
                s = s * (1.0 - noise) + nz * noise * 3.0
            s *= drive
            s = 1.0 if s > 1.0 else (-1.0 if s < -1.0 else s)
            env = t / atk if t < atk else ((1.0 - t) / rel if t > 1.0 - rel else 1.0)
            v = int(s * amp * env * VOL * 26000)
            v = 32767 if v > 32767 else (-32768 if v < -32768 else v)
            _buf[2 * c] = v; _buf[2 * c + 1] = v
            c += 1; i += 1
        i2s.write(_mv[:c * 2])

def silence(ms):
    n = RATE * ms // 1000
    for k in range(2 * _CH):
        _buf[k] = 0
    while n > 0:
        c = _CH if n > _CH else n
        i2s.write(_mv[:c * 2]); n -= c
# MP2_PART2
def scream():   # 咬人惨叫：高频尖叫带破音 + 下行哀嚎，粗糙失真
    play2(1500, 1300, 90,  parts=((1, 1), (2.01, 0.5)),            noise=0.25, drive=2.2, amp=0.9,
          vibHz=16, vibD=0.08, atk=0.005, rel=0.10)
    play2(1250, 520, 430,  parts=((1, 1), (1.01, 0.7), (0.5, 0.4)), noise=0.30, drive=2.6, amp=1.0,
          vibHz=13, vibD=0.10, pcurve=1.6, atk=0.004, rel=0.25)

def roar():     # 低沉咆哮：基频 + 次谐波 + 喉音颗粒 + 重失真
    play2(150, 60, 900, parts=((1, 1), (0.5, 0.8), (2, 0.3)), growlHz=48, growlD=0.7,
          noise=0.25, drive=2.8, amp=1.0, vibHz=5, vibD=0.06, pcurve=1.3, atk=0.01, rel=0.35)

def anger():    # 怒气满警告：三全音(不协和)急促报警 ×3
    for _ in range(3):
        play2(440, 440, 130, parts=((1, 1), (1.414, 0.9)), growlHz=20, growlD=0.5,
              drive=2.0, amp=0.85, atk=0.005, rel=0.10)
        silence(45)

def boot():     # 开机：阴森低鸣上行
    play2(70, 180, 700, parts=((1, 1), (1.5, 0.5), (2, 0.3)), growlHz=12, growlD=0.3,
          drive=1.6, amp=0.7, atk=0.08, rel=0.30)

def purr():     # 摸头：满足的低吼呼噜
    play2(85, 100, 700, parts=((1, 1), (2, 0.4)), growlHz=26, growlD=0.6,
          noise=0.10, drive=1.4, amp=0.55, atk=0.05, rel=0.30)

def clap():     # 拍手回应：好奇的短促低吼
    play2(300, 700, 160, parts=((1, 1), (1.5, 0.4)), growlHz=30, growlD=0.4,
          drive=1.8, amp=0.6, vibHz=10, vibD=0.05, atk=0.01, rel=0.20)

for name, fn in (("惨叫", scream), ("吼叫", roar), ("怒气警告", anger),
                 ("开机音", boot), ("摸头呼噜", purr), ("拍手回应", clap)):
    print(">>", name)
    fn(); gc.collect()
    time.sleep(0.6)
print(">> done")

