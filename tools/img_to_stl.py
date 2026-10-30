#!/usr/bin/env python3
import argparse
import struct
import math
import sys
from pathlib import Path

def load_rgb_image(image_path):
    """Return width, height, RGB pixel accessor.

    Mesher's existing lossless PNG decoder avoids a Pillow dependency for
    PNG test fixtures and CI. Pillow remains a fallback for other formats.
    """
    if Path(image_path).suffix.lower() == '.png':
        from png_to_rgba import read_png
        width, height, rgba = read_png(image_path)

        class Pixels:
            def __getitem__(self, xy):
                x, y = xy
                if not (0 <= x < width and 0 <= y < height):
                    raise IndexError(xy)
                offset = 4 * (y * width + x)
                return tuple(rgba[offset:offset+3])

        return width, height, Pixels()

    from PIL import Image
    image = Image.open(image_path).convert('RGB')
    width, height = image.size
    return width, height, image.load()


def generate_stretched_manifold_ellipsoid(image_path, out_path, radius=50.0):
    """
    Generates an outward-oriented, watertight manifold ellipsoid stretched
    along the pole-to-pole (Z) axis. The poles are unique vertices, not
    zero-radius latitude rings. Each quad is split into outward-facing ACB
    and BCD triangles; facet colouring remains tied to the same image pixels.
    """
    A = radius
    B = radius
    C = 1.6 * radius  # stretch along the pole-to-pole axis

    try:
        W, H, pixels = load_rgb_image(image_path)
    except Exception as e:
        print(f"Error opening image file: {e}", file=sys.stderr)
        sys.exit(1)

    Nx = W // 2  # Quads along image X (pole-to-pole axis)
    Ny = H // 2  # Quads along image Y (circumference loop axis)
    
    if Nx == 0 or Ny == 0:
        raise ValueError("Image resolution is too low to map 2x2 macro-pixel blocks.")
        

    # The end of the circumference folds exactly into the beginning for a manifold seal
    total_circumference_steps = Ny * 4  
    
    # Image X maps along the phi trajectory occupying the middle 75%
    phi_start = 0.25 * math.pi
    phi_end = 0.75 * math.pi
    
    num_cap_steps = 10  
    phi_array = []
    
    # Interior rings ONLY. A zero-radius ring at phi=0 consists of
    # coincident vertices, producing zero-area STL facets after welding.
    # The single true pole is connected by a triangle fan below.
    for step in range(1, num_cap_steps):
        phi_array.append((step / num_cap_steps) * phi_start)
    # Texture Zone
    for step in range(Nx + 1):
        phi_array.append(phi_start + (step / Nx) * (phi_end - phi_start))
    # Likewise exclude phi=pi: sin(pi) is a tiny nonzero floating-point
    # number, which would create a pathological sliver ring at the south pole.
    for step in range(1, num_cap_steps):
        phi_array.append(phi_end + (step / num_cap_steps) * (math.pi - phi_end))
        
    total_phi_points = len(phi_array)

    # Generate the 3D vertex coordinate grid
    grid = []
    for j in range(total_phi_points):
        phi = phi_array[j]
        row_vertices = []
        for i in range(total_circumference_steps):
            theta = (i / total_circumference_steps) * 2.0 * math.pi
            
            x = A * math.sin(phi) * math.cos(theta)
            y = B * math.sin(phi) * math.sin(theta)
            z = C * math.cos(phi)
            row_vertices.append((x, y, z))
        grid.append(row_vertices)

    # Watertight point coordinates for the physical caps
    north_pole_xyz = (0.0, 0.0, C)
    south_pole_xyz = (0.0, 0.0, -C)

    def rgb_to_magics_attr(r, g, b):
        r_5 = int(round(r * 31.0 / 255.0)) & 0x1F
        g_5 = int(round(g * 31.0 / 255.0)) & 0x1F
        b_5 = int(round(b * 31.0 / 255.0)) & 0x1F
        return r_5 | (g_5 << 5) | (b_5 << 10)

    fallback_attr = rgb_to_magics_attr(180, 180, 180)

    def calculate_normal(p1, p2, p3):
        ux, uy, uz = p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]
        vx, vy, vz = p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2]
        nx = uy*vz - uz*vy
        ny = uz*vx - ux*vz
        nz = ux*vy - uy*vx
        mag = math.sqrt(nx**2 + ny**2 + nz**2)
        if mag == 0: return (0.0, 0.0, 1.0)
        return (nx/mag, ny/mag, nz/mag)

    facets = []

    # North Pole triangle fan segment
    for i in range(total_circumference_steps):
        i_next = (i + 1) % total_circumference_steps
        v1 = north_pole_xyz
        v2 = grid[0][i]
        v3 = grid[0][i_next]
        n = calculate_normal(v1, v2, v3)
        facets.append((n, v1, v2, v3, fallback_attr))

    # Main structural grid loop
    for j in range(total_phi_points - 1):
        # One fewer ring on the north cap after removing the duplicate pole.
        first_texture_ring = num_cap_steps - 1
        in_texture_phi = (j >= first_texture_ring) and (j < first_texture_ring + Nx)
        img_x_idx = j - first_texture_ring if in_texture_phi else None
        
        for i in range(total_circumference_steps):
            i_next = (i + 1) % total_circumference_steps
            in_texture_theta = (i < Ny)
            img_y_idx = i if in_texture_theta else None
            
            v_tl = grid[j][i]        # A
            v_tr = grid[j][i_next]   # B
            v_bl = grid[j+1][i]      # C
            v_br = grid[j+1][i_next] # D
            
            if img_x_idx is not None and img_y_idx is not None:
                pix_x = 2 * img_x_idx
                pix_y = 2 * img_y_idx
                
                a = pixels[pix_x, pix_y]
                b = pixels[pix_x + 1, pix_y]
                c = pixels[pix_x, pix_y + 1]
                d = pixels[pix_x + 1, pix_y + 1]
                
                # Triangle 1 color: (a + b + c) / 3
                r_abc = (a[0] + b[0] + c[0]) / 3.0
                g_abc = (a[1] + b[1] + c[1]) / 3.0
                b_abc = (a[2] + b[2] + c[2]) / 3.0
                attr_abc = rgb_to_magics_attr(r_abc, g_abc, b_abc)
                
                # Triangle 2 color: (b + d + c) / 3 -> mathematically identical to (b + c + d) / 3
                r_bdc = (b[0] + d[0] + c[0]) / 3.0
                g_bdc = (b[1] + d[1] + c[1]) / 3.0
                b_bdc = (b[2] + d[2] + c[2]) / 3.0
                attr_bdc = rgb_to_magics_attr(r_bdc, g_bdc, b_bdc)
            else:
                attr_abc = fallback_attr
                attr_bdc = fallback_attr

            # X increases towards the south pole and Y increases along
            # the circumference, so A-B-C and B-D-C point INWARD.
            # Reverse BOTH faces to match the outward-facing pole fans.
            # The face/colour association is unchanged.
            n_acb = calculate_normal(v_tl, v_bl, v_tr)
            facets.append((n_acb, v_tl, v_bl, v_tr, attr_abc))

            n_bcd = calculate_normal(v_tr, v_bl, v_br)
            facets.append((n_bcd, v_tr, v_bl, v_br, attr_bdc))

    # South Pole triangle fan segment
    last_ring_idx = total_phi_points - 1
    for i in range(total_circumference_steps):
        i_next = (i + 1) % total_circumference_steps
        v1 = grid[last_ring_idx][i]
        v2 = south_pole_xyz
        v3 = grid[last_ring_idx][i_next]
        n = calculate_normal(v1, v2, v3)
        facets.append((n, v1, v2, v3, fallback_attr))

    # Validate the EXACT float32 coordinates that STL serializes, rather
    # than just the higher-precision Python grid. Reject degenerate facets,
    # inverted normals and edges that are not paired with opposite winding.
    # This prevents publishing a file that only appears healthy in a viewer
    # that silently drops redundant or nearly zero-area facets.
    edge_uses = {}
    unique_vertices = set()
    for face_index, (_normal, a, b, c, _colour) in enumerate(facets):
        corners = tuple(
            struct.unpack('<fff', struct.pack('<fff', *v))
            for v in (a, b, c)
        )
        if len(set(corners)) != 3:
            raise ValueError(f"STL facet {face_index} collapses after float32 welding")
        p, q, t = corners
        u = tuple(q[k] - p[k] for k in range(3))
        v = tuple(t[k] - p[k] for k in range(3))
        normal = (
            u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0],
        )
        if sum(n * n for n in normal) <= (
            1e-12 * math.sqrt(sum(x * x for x in u))
                  * math.sqrt(sum(x * x for x in v))
        ) ** 2:
            raise ValueError(f"STL facet {face_index} has zero effective area")
        centre = tuple((p[k] + q[k] + t[k]) / 3.0 for k in range(3))
        if sum(normal[k] * centre[k] for k in range(3)) <= 0:
            raise ValueError(f"STL facet {face_index} faces inward")
        unique_vertices.update(corners)
        for source, destination in ((p, q), (q, t), (t, p)):
            key = tuple(sorted((source, destination)))
            uses = edge_uses.setdefault(key, [0, 0])
            uses[0 if source < destination else 1] += 1

    invalid_edges = sum(uses != [1, 1] for uses in edge_uses.values())
    if invalid_edges:
        raise ValueError(
            f"STL has {invalid_edges} open, nonmanifold or inconsistently wound edges"
        )

    # Stream out only after validation succeeds.
    with open(out_path, 'wb') as f:
        header = b"COLOR=\xaa\xaa\xaa\xff 2x Stretched Fixed Normal Ellipsoid"
        header = header.ljust(80, b'\x00')[:80]
        f.write(header)
        
        f.write(struct.pack('<I', len(facets)))
        for normal, v1, v2, v3, attr in facets:
            data = struct.pack(
                '<ffffffffffffH',
                normal[0], normal[1], normal[2],
                v1[0], v1[1], v1[2],
                v2[0], v2[1], v2[2],
                v3[0], v3[1], v3[2],
                attr
            )
            f.write(data)

    print(
        f"Exported validated watertight mesh: {len(facets)} facets, "
        f"{len(unique_vertices)} welded float32 vertices, "
        f"{len(edge_uses)} paired edges."
    )

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Map an image onto a manifold ellipsoid with aligned normals.")
    parser.add_argument("-i", "--image", required=True, help="Path to input image file.")
    parser.add_argument("-o", "--output", default="output.stl", help="Output filename.")
    parser.add_argument("-r", "--radius", type=float, default=50.0, help="Equatorial radius.")
    
    args = parser.parse_args()
    generate_stretched_manifold_ellipsoid(args.image, args.output, args.radius)
