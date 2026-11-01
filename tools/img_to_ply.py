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
        triangles.append(((last_ring + i, south,
                         last_ring + (i + 1) % circumference))

    # Validate all geometry before publishing bytes. Each geometric edge has
    # exactly two incidences in opposite directions, with outward normals.
    if len(set(vertices)) != len(vertices):
        raise ValueError$VÆÆ—6ö–B†2GWÆ–6FR4BfW'F–6W2r¢VFvW2ÒFVfVÇFF–7B†ÆÖ&F¢³ÂÒ¢f÷"f6Uö–æFW‚Â–G2–âVçVÖW&FR‡G&–ævÆW2“ ¢Â"Â2Ò‡fW'F–6W5¶åÒf÷"â–â–G2¢RÒ†%³ÒÖ³ÒÂ%³ÒÖ³ÒÂ%³%ÒÖ³%Ò¢bÒ†5³ÒÖ³ÒÂ5³ÒÖ³ÒÂ5³%ÒÖ³%Ò¢æ÷&ÖÂÒ‡U³Ò§e£%Ò×U³%Ò§e³ÒÀ¢U³%Ò§e³Ò×U³Ò§e³%ÒÀ¢U³Ò§e³Ò×U³Ò§e³Ò¢6VçG&RÒGWÆR‚†¶µÒ¶%¶µÒ¶5¶µÒ’ó2ãf÷"²–â&ævRƒ2’¢–bæ÷BÆÂ†ÖF‚æ—6f–æ—FR‡‚’f÷"‚–â‚¦Â¦"Â¦2Â¦æ÷&ÖÂ’“ ¢&—6RfÇVTW'&÷"†btæöæf–æ—FR6ö÷&F–æFW2öæ÷&ÖÇ2Bf6R¶f6Uö–æFW‡Òr¢–b7VÒ†æ÷&ÖÅ¶µÒ¢6VçG&U¶µÒf÷"²–â&ævRƒ2’’ÃÒ ¢&—6RfÇVTW'&÷"†btFVvVæW&FRö–çv&BG&–ævÆR¶f6Uö–æFW‡Òr¢f÷"f—'7BÂ6V6öæB–â‚†–G5³ÒÂ–G5³Ò’À¢†–G5³ÒÂ–G5³%Ò’À¢†–G5³%ÒÂ–G5³Ò’“ ¢¶W’Ò†Ö–â†f—'7BÂ6V6öæB’ÂÖ‚†f—'7BÂ6V6öæB’¢VFvW5¶¶W•Õ³–bf—'7BÂ6V6öæBVÇ6RÒ³Ò¢–bç’‡W6W2Ò³ÂÒf÷"W6W2–âVFvW2çfÇVW2‚’“ ¢&—6RfÇVTW'&÷"‚tVÆÆ—6ö–B†2÷Vâ÷"–æ6öç6—7FVçBÖæ–föÆBVFvW2r ¢†VFW"Ò€¢wÇ•Ææf÷&ÖB&–æ'•öÆ—GFÆUöVæF–âãÆâp¢v6öÖÖVçBÖW6†W"–ÖvR×Fò×fW'FW‚Ö6öÆ÷W"VÆÆ—6ö–EÆâp¢bvVÆVÖVçBfW'FW‚¶ÆVâ‡fW'F–6W2—ÕÆâp¢w&÷W'G’F÷V&ÆR…Æç&÷W'G’F÷V&ÆR•Æç&÷W'G’F÷V&ÆR¥Æâp¢w&÷W'G’V6†"&VEÆç&÷W'G’V6†"w&VVåÆç&÷W'G’V6†"&ÇVUÆâp¢bvVÆVÖVçBf6R¶ÆVâ‡G&–ævÆW2—ÕÆâp¢w&÷W'G’Æ—7BV6†"–çBfW'FW…ö–æF–6W5ÆæVæEö†VFW%Æâp¢’æVæ6öFR‚v66–’r¢v—F‚÷Vâ†÷WE÷F‚Âwv"r’2f–ÆS ¢f–ÆRçw&—FR††VFW"¢f÷"ö–çBÂ6öÆ÷W"–â¦—‡fW'F–6W2Â6öÆ÷'2“ ¢f–ÆRçw&—FR…dU%DU…õ$T4õ$Bç6²‚§ö–çBÂ¦6öÆ÷W"’¢f÷"G&–ævÆR–âG&–ævÆW3 ¢f–ÆRçw&—FR„d4Uõ$T4õ$Bç6²ƒ2Â§G&–ævÆR’ ¢&–çB†btW‡÷'FVBÅ“¢¶ÆVâ‡fW'F–6W2—ÒfW'F–6W2Â¶ÆVâ‡G&–ævÆW2—Òf6W2Âp¢bw·v–GF‚¢†V–v‡GÒ–ÖvRÖ6öÆ÷W&VBfW'F–6W2Â¶ÆVâ†VFvW2—Ò—&VBVFvW2âr  ¦FVbÖ–â‚“ ¢'6W"Ò&w'6Rä&wVÖVçE'6W"€¢FW67&—F–öãÒuw&â–ÖvR&÷VæB'BöbvFW'F–v‡BVÆÆ—6ö–B–âÅ’fW'FW‚$t"âr¢'6W"æFEö&wVÖVçB‚rÖ’rÂrÒÖ–ÖvRrÂ&WV—&VCÕG'VRÂ†VÇÒu6÷W&6Rär÷"–ÆÆ÷r×7W÷'FVB–ÖvRr¢'6W"æFEö&wVÖVçB‚rÖòrÂrÒÖ÷WGWBrÂFVfVÇCÒv÷WGWBçÇ’rÂ†VÇÒtFW7F–æF–öâÅ’r¢'6W"æFEö&wVÖVçB‚r×"rÂrÒ×&F—W2rÂG—SÖfÆöBÂFVfVÇCÓSãÀ¢†VÇÒtWVF÷&–Â&F—W2‡÷6—F—fRÂf–æ—FR’r¢&w2Ò'6W"ç'6Uö&w2‚¢vVæW&FU÷7G&WF6†VEöÖæ–föÆEöVÆÆ—6ö–B†&w2æ–ÖvRÂ&w2æ÷WGWBÂ&w2ç&F—W2  ¦–bõöæÖUõòÓÒuõöÖ–åõòs ¢Ö–â‚ 