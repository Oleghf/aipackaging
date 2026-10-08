"""Создаёт собственный значок листа с деталями в формате ICO без внешних библиотек."""
from pathlib import Path
import struct
import sys


def make_icon(destination: Path) -> None:
    """Записывает значок 32×32; не использует сторонние изображения и сетевые ресурсы."""
    size = 32
    pixels = bytearray()
    for y in reversed(range(size)):
        for x in range(size):
            color = (0, 0, 0, 0)
            if 3 <= x <= 28 and 2 <= y <= 29:
                color = (245, 245, 245, 255)
            if (6 <= x <= 15 and 5 <= y <= 17) or (18 <= x <= 25 and 5 <= y <= 12) or (6 <= x <= 25 and 20 <= y <= 26):
                color = (180, 100, 30, 255)
            pixels.extend(color)
    bitmap = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(pixels), 0, 0, 0, 0)
    bitmap += pixels + bytes(size * 4)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', size, size, 0, 0, 1, 32, len(bitmap), 22) + bitmap)


if __name__ == '__main__':
    make_icon(Path(sys.argv[1]))
