#!/usr/bin/env python3
"""GPU-free smoke coverage for the dependency-free packed PNG assets."""
import importlib.util
import struct
import tempfile
import zlib
from pathlib import Path

script = Path(__file__).resolve().parent.parent / "tools" / "png_to_rgba.py"
spec = importlib.util.spec_from_file_location("png_to_rgba", script)
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data +
            struct.pack(">I", zlib.crc32(kind + data)))


def png(width, height, color, rows):
    header = struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0)
    return (converter.SIGNATURE + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(b"".join(rows))) +
            chunk(b"IEND", b""))


def main():
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "input.png"
        output = Path(directory) / "output.rgba"
        # Two rows with transparent and opaque pixels; exercise None/Up.
        source.write_bytes(png(2, 2, 6, (
            bytes([0, 255, 0, 0, 255, 0, 255, 0, 64]),
            bytes([2, 0, 0, 255, 0, 255, 0, 0, 191]),
        )))
        width, height, image = converter.read_png(source)
        assert (width, height) == (2, 2)
        assert image == bytes((
            255, 0, 0, 255, 0, 255, 0, 64,
            255, 0, 255, 255, 255, 255, 0, 255,
        ))
        converter_path = output
        converter_path.write_bytes(struct.pack("<II", width, height) + image)
        packed = output.read_bytes()
        assert struct.unpack_from("<II", packed) == (2, 2)
        assert packed[8:] == image
        # Invalid/incomplete PNG cannot silently yield a blank splash.
        source.write_bytes(b"not a PNG")
        try:
            converter.read_png(source)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid PNG must be rejected")
    print("PNG -> packed RGBA asset tests passed")


if __name__ == "__main__":
    main()
