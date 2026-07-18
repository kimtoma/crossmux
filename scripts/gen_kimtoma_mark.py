#!/usr/bin/env python3
"""Generate the approved 120x120 @kimtoma boot/sleep mark.

The SVG is intentionally a set of integer-aligned black rectangles. It is a
compact vector trace of the approved pure 1-bit sample, so standard-library
XML parsing produces the exact same panel pixels on every machine.

Firmware format: row-major, MSB-first, white bit=1, black bit=0. The packed
bytes are pre-rotated 90 degrees counter-clockwise because drawImage() blits
directly to the X4's landscape framebuffer, which is presented 90 degrees
clockwise in the logical portrait screen.
"""

import argparse
import pathlib
import struct
import xml.etree.ElementTree as ET
import zlib


SIZE = 120


def build_rows(svg_path):
    root = ET.parse(svg_path).getroot()
    if root.get("viewBox") != "0 0 120 120":
        raise ValueError("Kimtoma mark SVG must use viewBox 0 0 120 120")

    rows = [[1] * SIZE for _ in range(SIZE)]
    rectangles = 0
    for element in root.iter():
        if element.tag.rsplit("}", 1)[-1] != "rect" or element.get("fill") != "#000000":
            continue
        x = int(element.get("x", "0"))
        y = int(element.get("y", "0"))
        width = int(element.get("width", "0"))
        height = int(element.get("height", "0"))
        if width <= 0 or height <= 0 or x < 0 or y < 0 or x + width > SIZE or y + height > SIZE:
            raise ValueError("Kimtoma mark contains an out-of-bounds rectangle")
        rectangles += 1
        for row in range(y, y + height):
            rows[row][x:x + width] = [0] * width
    if rectangles == 0:
        raise ValueError("Kimtoma mark SVG contains no black geometry")
    return rows


def pack(rows):
    physical_rows = [
        [rows[x][SIZE - 1 - y] for x in range(SIZE)]
        for y in range(SIZE)
    ]
    packed = []
    for row in physical_rows:
        for x in range(0, SIZE, 8):
            byte = 0
            for offset in range(8):
                if row[x + offset]:
                    byte |= 1 << (7 - offset)
            packed.append(byte)
    return packed


def header_text(packed):
    tokens = [f"0x{value:02x}" for value in packed]
    body = []
    for index in range(0, len(tokens), 19):
        line = "    " + ", ".join(tokens[index:index + 19])
        line += "};" if index + 19 >= len(tokens) else ","
        body.append(line)
    return (
        "#pragma once\n#include <cstdint>\n\n"
        "// Image dimensions: 120x120\n"
        "// Source: src/images/KimtomaMark120.svg - regenerate via scripts/gen_kimtoma_mark.py\n"
        "static const uint8_t KIMTOMA_MARK_120[] = {\n"
        + "\n".join(body)
        + "\n"
    )


def write_header(packed, path):
    pathlib.Path(path).write_text(header_text(packed), encoding="utf-8")


def write_png(rows, path, scale=2):
    width = height = SIZE * scale
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        source_y = y // scale
        for x in range(width):
            raw.append(255 if rows[source_y][x // scale] else 0)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    pathlib.Path(path).write_bytes(png)


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--svg", type=pathlib.Path, default=root / "src/images/KimtomaMark120.svg")
    parser.add_argument("--header", type=pathlib.Path, default=root / "src/images/KimtomaMark120.h")
    parser.add_argument("--png", type=pathlib.Path)
    args = parser.parse_args()

    rows = build_rows(args.svg)
    write_header(pack(rows), args.header)
    print(f"Wrote {args.header}")
    if args.png:
        write_png(rows, args.png)
        print(f"Wrote {args.png}")


if __name__ == "__main__":
    main()
