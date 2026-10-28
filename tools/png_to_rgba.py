#!/usr/bin/env python3
"""Convert a standard PNG to an endian-stable width/height + RGBA8 asset.

No third-party Python modules or runtime PNG libraries are needed by Mesher.
PNG scanlines use the standard five lossless filters; Adam7 is supported.
"""
import argparse
import struct
import zlib
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"
PASSES = ((0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8),
          (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))


def read_png(path):
    data = Path(path).read_bytes()
    if not data.startswith(SIGNATURE):
        raise ValueError(f"{path}: not a PNG")
    offset = 8
    ihdr = None
    palette = None
    transparency = b""
    compressed = bytearray()
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        payload = data[offset + 8:offset + 8 + length]
        if zlib.crc32(kind + payload) != struct.unpack_from(">I", data, end - 4)[0]:
            raise ValueError(f"{path}: corrupt PNG chunk")
        offset = end
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", payload)
        elif kind == b"PLTE":
            palette = payload
        elif kind == b"tRNS":
            transparency = payload
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    if ihdr is None:
        raise ValueError(f"{path}: missing IHDR")
    width, height, depth, color, compression, filtering, interlace = ihdr
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if (not width or not height or width > 8192 or height > 8192
            or depth != 8 or channels is None or compression or filtering
            or interlace not in (0, 1)):
        raise ValueError(f"{path}: unsupported PNG dimensions/format")
    raw = memoryview(zlib.decompress(compressed))
    cursor = 0
    rgba = bytearray(width * height * 4)

    def visit(x0, y0, sx, sy):
        nonlocal cursor
        w = (width - x0 + sx - 1) // sx if x0 < width else 0
        h = (height - y0 + sy - 1) // sy if y0 < height else 0
        if not w or not h:
            return
        stride = w * channels
        previous = bytearray(stride)
        for y in range(h):
            if cursor + stride + 1 > len(raw):
                raise ValueError(f"{path}: truncated filtered scanlines")
            filter_type = raw[cursor]
            cursor += 1
            row = bytearray(raw[cursor:cursor + stride])
            cursor += stride
            if filter_type not in range(5):
                raise ValueError(f"{path}: bad PNG filter")
            for i in range(stride):
                a = row[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                if filter_type == 1:
                    predictor = a
                elif filter_type == 2:
                    predictor = b
                elif filter_type == 3:
                    predictor = (a + b) // 2
                elif filter_type == 4:
                    p = a + b - c
                    distances = (abs(p - a), abs(p - b), abs(p - c))
                    predictor = (a, b, c)[distances.index(min(distances))]
                else:
                    predictor = 0
                row[i] = (row[i] + predictor) & 255
            previous = row
            for x in range(w):
                i = x * channels
                if color == 6:
                    pixel = row[i:i + 4]
                elif color == 2:
                    pixel = (*row[i:i + 3], 255)
                elif color == 0:
                    pixel = (row[i], row[i], row[i], 255)
                elif color == 4:
                    pixel = (row[i], row[i], row[i], row[i + 1])
                else:
                    index = row[i]
                    if palette is None or 3 * index + 3 > len(palette):
                        raise ValueError(f"{path}: invalid PNG palette")
                    pixel = (*palette[3 * index:3 * index + 3],
                             transparency[index] if index < len(transparency) else 255)
                j = 4 * ((y0 + y * sy) * width + x0 + x * sx)
                rgba[j:j + 4] = bytes(pixel)

    if interlace:
        for args in PASSES:
            visit(*args)
    else:
        visit(0, 0, 1, 1)
    if cursor != len(raw):
        raise ValueError(f"{path}: unexpected extra PNG image data")
    return width, height, rgba


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source")
    parser.add_argument("output")
    args = parser.parse_args()
    width, height, pixels = read_png(args.source)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(struct.pack("<II", width, height) + pixels)
    print(f"{args.source}: {width}x{height} RGBA")


if __name__ == "__main__":
    main()
