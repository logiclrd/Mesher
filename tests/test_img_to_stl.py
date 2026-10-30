"""Positive regression for pole-cap STL generation; no Pillow dependency."""
import importlib.util
from pathlib import Path
import sys
import types


class SyntheticImage:
    size = (8, 8)

    def convert(self, mode):
        assert mode == "RGB"
        return self

    def load(self):
        class Pixels:
            def __getitem__(self, xy):
                x, y = xy
                return ((x * 29 + y * 7) % 256,
                        (x * 13 + y * 41) % 256,
                        (x * 11 + y * 3) % 256)
        return Pixels()


def main():
    script = Path(sys.argv[1])
    output = Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)

    fake_pil = types.ModuleType("PIL")
    fake_pil.Image = types.SimpleNamespace(open=lambda _: SyntheticImage())
    sys.modules["PIL"] = fake_pil

    spec = importlib.util.spec_from_file_location("mesher_img_to_stl", script)
    source = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source)
    source.generate_stretched_manifold_ellipsoid("synthetic-image", output,
                                                 radius=5.0)

    import struct
    data = output.read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    # 16 circumference divisions * (22 latitude bands * 2 + 2 pole fans)
    assert count == 736, count
    assert len(data) == 84 + 50 * count
    attrs = {struct.unpack_from("<H", data, 84 + i * 50 + 48)[0]
             for i in range(count)}
    assert len(attrs) > 2, "Synthetic image should survive as facet colours"


if __name__ == "__main__":
    main()
