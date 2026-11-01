"""Exact pixel-to-vertex PLY regression with deterministic RGB, no Pillow."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import zlib
import sys
import types


class SyntheticImage:
    size = (8, 8)

    def convert(self, mode):
        assert mode == 'RGB'
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
    script = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    fake_pil = types.ModuleType('PIL')
    fake_pil.Image = types.SimpleNamespace(open=lambda _: SyntheticImage())
    sys.modules['PIL'] = fake_pil
    sys.path.insert(0, str(script.parent))
    spec = importlib.util.spec_from_file_location('mesher_img_to_ply', script)
    source = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source)
    source.generate_stretched_manifold_ellipsoid('synthetic-image', output,
                                                 radius=5.0)

    raw = output.read_bytes()
    header, body = raw.split(b'end_header\n', 1)
    header += b'end_header\n'
    circumference = 4 * (SyntheticImage.size[1] - 1)
    ring_count = SyntheticImage.size[0] + 2 * (source.CAP_STEPS - 1)
    expected_vertices = ring_count * circumference + 2
    expected_faces = 2 * (expected_vertices - 2)
    assert b'format binary_little_endian 1.0' in header
    assert b'property double x' in header
    assert b'property uchar red' in header
    assert b'property uchar green' in header
    assert b'property uchar blue' in header
    assert b'property uchar alpha' not in header
    assert f'element vertex {expected_vertices}'.encode() in header
    assert f'element face {expected_faces}'.encode() in header
    assert len(body) == expected_vertices * source.VERTEX_RECORD.size + expected_faces * source.FACE_RECORD.size
    position = lambda n: body[n*source.VERTEX_RECORD.size:(n+1)*source.VERTEX_RECORD.size]
    records = [source.VERTEX_RECORD.unpack(position(i)) for i in range(expected_vertices)]
    samples = SyntheticImage().load()
    for x in range(SyntheticImage.size[0]):
        for y in range(SyntheticImage.size[1]):
            i = (source.CAP_STEPS-1 + x) * circumference + y
            assert records[i][3:] == samples[x, y], (x, y, records[i][3:])
    assert records[0][3:] == source.BACKGROUND
    assert records[-1][3:] == source.BACKGROUND
    assert records[-2][:3] == (7.0, 0.0, 0.0)
    assert records[-1][:3] == (-7.0, 0.0, 0.0)
    edges = {}
    offset = expected_vertices * source.VERTEX_RECORD.size
    for i in range(expected_faces):
        count, a, b, c = source.FACE_RECORD.unpack_from(body, offset + i*source.FACE_RECORD.size)
        assert count == 3 and len({a, b, c}) == 3
        assert all(0 <= j < expected_vertices for j in (a,b,c))
        for first, second in ((a,b),(b,c),(c,a)):
            key = tuple(sorted((first,second)))
            uses = edges.setdefault(key, [0, 0])
            uses[0 if first < second else 1] += 1
    assert all(uses == [1,1] for uses in edges.values())
    # Exercise the CLI and the dependency-free PNG reader, too. The same
    # patterned pixels must land on the same vertices without Pillow.
    png = output.with_suffix('.png')
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack(
            '>I', zlib.crc32(name + data))
    rows = b''.join(b'\x00' + bytes(component
        for x in range(8) for component in samples[x, y])
        for y in range(8))
    png.write_bytes(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', 8, 8, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
    command_output = output.with_name('cli-fixture.ply')
    # A subprocess starts with no injected fake PIL; PNG support must work
    # through the repository's existing standard-library decoder.
    subprocess.run([sys.executable, str(script), '-i', str(png),
                    '-o', str(command_output), '-r', '5'], check=True)
    assert command_output.read_bytes() == output.read_bytes(), (
        'CLI PNG path changed the exact vertex colours or surface')
    print('Image-to-PLY pixel fidelity, binary layout, CLI PNG and closed manifold passed')


if __name__ == '__main__':
    main()
