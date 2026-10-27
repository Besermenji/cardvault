#!/usr/bin/env python3
"""Capture CardVault screenshots from a debug build (env:cardputer-debug).

Press ctrl+s on the Cardputer; each dump is saved as a PNG.

  python3 tools/screenshot.py /dev/cu.usbmodem1101 [out_dir]   # live, needs pyserial
  python3 tools/screenshot.py capture.log [out_dir]            # from a saved serial log

The debug build uses the test partition table (empty demo vault, its own "cvtvault" partition label), so it
cannot open a real vault. Use dummy data only: screenshots can contain revealed secrets.
"""
import pathlib
import struct
import sys
import time
import zlib


def rgb565_to_rgb(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter: none
        for i in range(0, len(row), 4):
            raw.extend(rgb565_to_rgb(int(row[i:i + 4], 16)))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    pathlib.Path(path).write_bytes(png)


def lines_from(source):
    p = pathlib.Path(source)
    if p.is_file():
        yield from p.read_text(errors="replace").splitlines()
        return
    import serial  # pyserial

    with serial.Serial(source, 115200, timeout=1) as port:
        print(f"Listening on {source} - press ctrl+s on the Cardputer (ctrl+c to quit)")
        while True:
            line = port.readline().decode(errors="replace").strip()
            if line:
                yield line


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "screenshots")
    out.mkdir(parents=True, exist_ok=True)
    rows, size = None, None
    for line in lines_from(sys.argv[1]):
        if line.startswith("CVSHOT"):
            _, w, h = line.split()
            size, rows = (int(w), int(h)), []
        elif line == "CVEND" and rows is not None:
            if len(rows) == size[1]:
                name = out / time.strftime("cardvault-%Y%m%d-%H%M%S.png")
                if name.exists():
                    name = out / f"{name.stem}-{len(list(out.iterdir()))}.png"
                write_png(name, size[0], size[1], rows)
                print(f"saved {name}")
            rows = None
        elif rows is not None and len(line) == size[0] * 4:
            rows.append(line)


if __name__ == "__main__":
    main()
