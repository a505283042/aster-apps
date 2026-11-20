"""Bake compact outlines for the two faces of the 460px paper lyric renderer."""

import argparse
import math
import struct
from pathlib import Path

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont


def codepoints():
    points = set(range(32, 256))
    for start, end in ((0x2000, 0x2070), (0x3000, 0x3040), (0xFF00, 0xFFF0)):
        points.update(range(start, end))
    for first in range(0xA1, 0xF8):
        for second in range(0xA1, 0xFF):
            ch = bytes((first, second)).decode("gb2312", errors="ignore")
            if ch:
                points.add(ord(ch))
    return points


class Outline(BasePen):
    """Flatten quadratic outlines with subpixel accuracy at the largest display em."""

    def __init__(self, glyphs, units, width):
        super().__init__(glyphs)
        self.scale = 255 * 76 / (80 * units)
        self.center = width / 2
        self.baseline = units * 0.366
        self.contours = []
        self.current = []

    def _lineTo(self, point):
        x, y = point
        p = tuple(max(0, min(255, round(v))) for v in
                  (127.5 + (x - self.center) * self.scale,
                   127.5 + (self.baseline - y) * self.scale))
        if not self.current or self.current[-1] != p:
            self.current.append(p)

    _moveTo = _lineTo

    def _qCurveToOne(self, control, end):
        start = self._getCurrentPoint()
        curvature = math.hypot(*(a - 2*b + c for a, b, c in zip(start, control, end)))
        steps = max(1, math.ceil(math.sqrt(curvature * self.scale / 2)))
        for i in range(1, steps + 1):
            t = i / steps
            self._lineTo(tuple((1-t)**2*a + 2*t*(1-t)*b + t*t*c
                               for a, b, c in zip(start, control, end)))

    def _closePath(self):
        if self.current and self.current[-1] == self.current[0]:
            self.current.pop()
        if len(self.current) >= 3:
            self.contours.append(self.current)
        self.current = []

    _endPath = _closePath


def bake(path, out, weight):
    with TTFont(path) as source:
        glyphs = source.getGlyphSet(location={"wght": weight})
        cmap = source.getBestCmap()
        points = sorted(codepoints() & cmap.keys())
        index = bytearray()
        data = bytearray()
        for point in points:
            glyph = glyphs[cmap[point]]
            pen = Outline(glyphs, source["head"].unitsPerEm, glyph.width)
            glyph.draw(pen)
            start = len(data)
            for contour in pen.contours:
                data.extend(struct.pack("<H", len(contour)))
                for vertex in contour:
                    data.extend(vertex)
            length = len(data) - start
            if length > 4096:
                raise ValueError(f"Glyph U+{point:04X} exceeds decoder packet size")
            index.extend(struct.pack("<HHI", point, length, start))
    out.write_bytes(struct.pack("<4sHH", b"ALV1", 80, len(points)) + index + data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serif", type=Path, help="Noto Serif SC variable font")
    parser.add_argument("sans", type=Path, help="Noto Sans SC variable font")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "text")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    bake(args.serif, args.out / "paper-serif.bin", 400)
    bake(args.sans, args.out / "paper-bold.bin", 700)


if __name__ == "__main__":
    main()
