"""Generate the LRC decoder's little-endian GBK-to-Unicode map."""
from pathlib import Path
import struct


def codepoint(lead, trail):
    try:
        return ord(bytes((lead, trail)).decode("gbk"))
    except UnicodeDecodeError:
        return 0


if __name__ == "__main__":
    output = Path(__file__).resolve().parents[1] / "text" / "gbk.bin"
    output.parent.mkdir(exist_ok=True)
    trails = (*range(0x40, 0x7F), *range(0x80, 0xFF))
    output.write_bytes(b"".join(struct.pack("<H", codepoint(a, b))
                                for a in range(0x81, 0xFF) for b in trails))
