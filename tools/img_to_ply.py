#!/usr/bin/env python3
"""Map image pixels to ellipsoid vertices in a coloured, watertight PLY mesh.

The shape and texture placement match img_to_stl.py: the image occupies the
middle 50% of the polar angle and one quarter of the circumference. Unlike the
facet-colour STL, each pixel becomes the exact RGB of one distinct vertex; no
2x2-pixel blocks, averaging, or five-bit quantization are involved.
"""

import argparse
from collections import defaultdict
import math
import struct

from img_to_stl import load_rgb_image


VERTEX_RECORD = struct.Struct('<dddBBB')
FACE_RECORD = struct.Struct('<Biii')
CAP_STEPS = 10
BACKGROUND = (180, 180, 180)
MAX_VERTICES = 1_000_000  # Mesher PLY import limits
MAX_FACES = 2_000_000
MAX_BYTES = 256 * 1024 * 1024


def generate_stretched_manifold_ellipsoid(image_path, out_path, radius=50.0):
    """Write a Mesher-readable binary LE PLY, preserving every source RGB pixel.

    Pixel (x, y) maps to vertex (CAP_STEPS-1+x, y) in the ring grid.
    The remaining rings and the other 3/4 of the circumference use grey RGB.
    Two polar vertices close the surface without degenerate pole rings.
    """
    if not math.isfinite(radius) or radius <= 0:
        raise ValueError('Radius must be a positive finite number')
    width, height, pixels = load_rgb_image(image_path)
    if width < 2 or height < 2:
        raise ValueError('Image needs at least two pixels on each axis')

    # The (height-1) image intervals span exactly 1/4 of the circumference.
    circumference = 4 * (height - 1)
    phi_start = math.pi / 4.0
    phi_end = 3.0 * math.pi / 4.0
    phi = (
        [step / CAP_STEPS * phi_start for step in range(1, CAP_STEPS)]
        + [phi_start + col / (width - 1) * (phi_end - phi_start)
           for col in range(width)]
        + [phi_end + step / CAP_STEPS * (math.pi - phi_end)
           for step in range(1, CAP_STEPS)]
    )
    ring_count = len(phi)
    vertex_count = ring_count * circumference + 2
    face_count = 2 * (ring_count - 1) * circumference + 2 * circumference
    estimated_bytes = (vertex_count * VERTEX_RECORD.size
                       + face_count * FACE_RECORD.size + 512)
    if (vertex_count > MAX_VERTICES or face_count > MAX_FACES
            or estimated_bytes > MAX_BYTES):
        raise ValueError('Image produces a mesh exceeding Mesher PLY import limits')

    # Indexing order is j*circumference+i: increasing image X advances along
    # the polar trajectory, increasing image Y advances around the ellipsoid.
    vertices = []
    colors = []
    for j, angle in enumerate(phi):
        for i in range(circumference):
            theta = (0.3 + i / circumference) * (2.0 * math.pi)
            point = (1.4 * radius * math.cos(angle),
                     radius * math.sin(angle) * math.cos(theta),
                     radius * math.sin(angle) * math.sin(theta))
            vertices.append(point)
            if CAP_STEPS - 1 <= j < CAP_STEPS - 1 + width and i < height:
                colour = tuple(pixels[j - (CAP_STEPS - 1), i])
                if (len(colour) != 3 or
                        any(not isinstance(v, int) or not 0 <= v <= 255
                            for v in colour)):
                    raise ValueError('Image pixel must have three 8-bit RGB channels')
                colors.append(colour)
            else:
                colors.append(BACKGROUND)

    north = len(vertices)
    vertices.append((1.4 * radius, 0.0, 0.0))
    colors.append(BACKGROUND)
    south = len(vertices)
    vertices.append((-1.4 * radius, 0.0, 0.0))
    colors.append(BACKGROUND)

    triangles = []
    for i in range(circumference):
        triangles.append((north, i, (i + 1) % circumference))
    for j in range(ring_count - 1):
        for i in range(circumference):
            ni = (i + 1) % circumference
            a = j * circumference + i
            b = j * circumference + ni
            c = (j + 1) * circumference + i
            d = (j + 1) * circumference + ni
            # Reverse the inward A-B-C/B-D-C winding of an increasing grid.
            triangles.append((a, c, b))
            triangles.append((b, c, d))
    last_ring = (ring_count - 1) * circumference
    for i in range(circumference):
        triangles.append((last_ring + i, south,
                          last_ring + (i + 1) % circumference))

    # Validate all geometry before publishing bytes. Each geometric edge has
    # exactly two incidences in opposite directions, with outward normals.
    if len(set(vertices)) != len(vertices):
        raise ValueError('Ellipsoid has duplicate vertex positions')

    edges = defaultdict(lambda: [0, 0])
    for face_index, ids in enumerate(triangles):
        if len(set(ids)) != 3 or any(i < 0 or i >= len(vertices) for i in ids):
            raise ValueError(f'Invalid vertex indices in face {face_index}')
        a, b, c = (vertices[i] for i in ids)
        u = tuple(b[k] - a[k] for k in range(3))
        v = tuple(c[k] - a[k] for k in range(3))
        normal = (u[1] * v[2] - u[2] * v[1],
                  u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0])
        centre = tuple((a[k] + b[k] + c[k]) / 3.0 for k in range(3))
        if not all(math.isfinite(x) for x in (*a, *b, *c, *normal)):
            raise ValueError(f'Nonfinite coordinates at face {face_index}')
        if sum(normal[k] * centre[k] for k in range(3)) <= 0:
            raise ValueError(f'Face {face_index} is degenerate or faces inward')
        for first, second in ((ids[0], ids[1]), (ids[1], ids[2]),
                              (ids[2], ids[0])):
            edge = (min(first, second), max(first, second))
            edges[edge][0 if first < second else 1] += 1
    if any(uses != [1, 1] for uses in edges.values()):
        raise ValueError('Ellipsoid has open or inconsistently wound mesh edges')

    header = (
        'ply\nformat binary_little_endian 1.0\n'
        'comment Mesher image-to-vertex-colour ellipsoid\n'
        f'element vertex {len(vertices)}\n'
        'property double x\nproperty double y\nproperty double z\n'
        'property uchar red\nproperty uchar green\nproperty uchar blue\n'
        f'element face {len(triangles)}\n'
        'property list uchar int vertex_indices\nend_header\n'
    ).encode('ascii')

    with open(out_path, 'wb') as file:
        file.write(header)
        for position, colour in zip(vertices, colors):
            file.write(VERTEX_RECORD.pack(*position, *colour))
        for ids in triangles:
            file.write(FACE_RECORD.pack(3, *ids))

    print(f'Exported PLY: {len(vertices)} vertices, {len(triangles)} faces, '
          f'{width * height} image-coloured vertices, {len(edges)} paired edges.')


def main():
    parser = argparse.ArgumentParser(
        description='Wrap an image around part of a watertight ellipsoid in PLY vertex RGB.')
    parser.add_argument('-i', '--image', required=True,
                        help='Source PNG or Pillow-supported image')
    parser.add_argument('-o', '--output', default='output.ply',
                        help='Destination PLY')
    parser.add_argument('-r', '--radius', type=float, default=50.0,
                        help='Equatorial radius (positive, finite)')
    args = parser.parse_args()
    generate_stretched_manifold_ellipsoid(args.image, args.output, args.radius)


if __name__ == '__main__':
    main()
