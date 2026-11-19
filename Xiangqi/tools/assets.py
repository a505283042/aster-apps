"""Generate the original vector-style launcher icon and wooden-piece click."""

from pathlib import Path
import math, struct, wave, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parents[1] / 'aster-sdk/tools'))
from icon_convert import encode, CF_RGB565A8


def main():
    scale = 4
    im = Image.new('RGBA', (384, 384), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, 383, 383), radius=80, fill='#70362c')
    d.rounded_rectangle((20, 20, 363, 363), radius=66, outline='#cda664', width=5)
    for i in range(3):
        v = 72 + i * 120
        d.line((v, 44, v, 340), fill='#a16b43', width=3)
        d.line((44, v, 340, v), fill='#a16b43', width=3)
    d.ellipse((58, 67, 334, 343), fill='#452a23')
    d.ellipse((52, 49, 328, 325), fill='#c19150', outline='#f4dcac', width=5)
    d.ellipse((65, 62, 315, 312), fill='#f5dfae', outline='#8e3327', width=5)
    fonts = [
        'C:/Windows/Fonts/msyh.ttc',
        '/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc',
        '/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc',
    ]
    font = next((p for p in fonts if Path(p).exists()), None)
    if not font:
        raise RuntimeError('Set up a Chinese font for icon generation')
    d.text(
        (190, 183),
        '帅',
        font=ImageFont.truetype(font, 170),
        anchor='mm',
        fill='#9f2b23',
        stroke_width=1,
    )
    im = im.resize((96, 96), Image.Resampling.LANCZOS)
    (ROOT / 'icon.bin').write_bytes(encode(im, CF_RGB565A8))
    im.save(ROOT / 'icon.png')
    rate = 16000
    pcm = bytearray(b'\0' * 1280)
    noise = 0x5849414E
    for i in range(3200):
        t = i / rate
        noise = (1664525 * noise + 1013904223) & 0xFFFFFFFF
        click = ((noise >> 16) / 32768 - 1) * math.exp(-90 * t)
        tone = math.sin(2 * math.pi * 640 * t) * math.exp(-22 * t) + 0.4 * math.sin(
            2 * math.pi * 1130 * t
        ) * math.exp(-35 * t)
        value = int(23000 * (0.32 * click + 0.6 * tone) * min(1, t * 1800))
        pcm.extend(struct.pack('<h', max(-32768, min(32767, value))))
    out = ROOT / 'audio/move.wav'
    out.parent.mkdir(exist_ok=True)
    with wave.open(str(out), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(pcm)


def cues():
    rate = 16000
    for name in ('capture', 'check', 'end'):
        data = bytearray(b'\0' * 1280)
        duration = {'capture': 0.34, 'check': 0.7, 'end': 1.25}[name]
        for i in range(int(rate * duration)):
            t = i / rate
            v = 0
            if name == 'capture':
                for start, freq in [(0, 270), (0.115, 390)]:
                    u = t - start
                    if u >= 0:
                        v += (
                            0.8
                            * math.exp(-24 * u)
                            * (
                                math.sin(2 * math.pi * freq * u)
                                + 0.3 * math.sin(2 * math.pi * freq * 2.7 * u)
                            )
                            * min(1, u * 1600)
                        )
            elif name == 'check':
                v = (
                    min(1, t * 250)
                    * math.exp(-4 * t)
                    * (
                        math.sin(2 * math.pi * 590 * t)
                        + 0.32 * math.sin(2 * math.pi * 941 * t)
                        + 0.16 * math.sin(2 * math.pi * 1373 * t)
                    )
                )
            else:
                for j, freq in enumerate([523.25, 587.33, 783.99, 1046.5]):
                    u = t - j * 0.18
                    if u >= 0:
                        v += (
                            0.6
                            * min(1, u * 600)
                            * math.exp(-6 * u)
                            * (
                                math.sin(2 * math.pi * freq * u)
                                + 0.18 * math.sin(2 * math.pi * freq * 2 * u)
                            )
                        )
            value = int(20000 * v * min(1, (duration - t) * 80))
            data.extend(struct.pack('<h', max(-32767, min(32767, value))))
        with wave.open(str(ROOT / 'audio' / f'{name}.wav'), 'wb') as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(rate)
            f.writeframes(data)


if __name__ == '__main__':
    main()
    cues()
