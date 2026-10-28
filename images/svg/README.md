# Mesher SVG Advancing-Front Triangulator

A standalone Python adaptation of Mesher's M22B/M23B advancing-front capping algorithm, operating on SVG path polygons in 2D. It reads path subcommands as straight **chords between their endpoints** and adds a sibling `<g>` of `<path>` mesh edges and `<circle>` vertex markers.

## Install

```bash
pip install 'shapely>=2.1'
```

## Example

```bash
python mesher_svg_front.py mesher_m_outline.svg mesher_m_triangulated.svg \
  --max-edge 22 --line-width .85 --dot-radius 1.5
```

The default color is turquoise edges and white vertex circles; override with `--stroke '#ffffff' --dot '#111111'` or edit the generated SVG.

### Options

- `--max-edge N`: splits straight input boundary edges exceeding N SVG units into collinear segments. Original outline corners stay at exactly the same coordinates. Without this argument, original edges are not subdivided.
- `--line-width N`, `--dot-radius N`: display thickness/vertex radius (SVG coordinate units).
- `--stroke COLOR`, `--dot COLOR`: edge and vertex colors.
- `--preserve-source`: leave original path commands untouched rather than replacing curves/arcs with straight `L` chords. **This can make curves disagree with the chord-based mesh!**
- `--max-vertices N`, `--max-faces N`: safety limits.

## Geometry and rendering

1. Parse every visible `<path>` (except paths inside `<defs>`, `<clipPath>`, `<mask>`, `<symbol>`, `<marker>`, or `<pattern>`). Drawing primitives `<rect>`, `<polygon>`, `<text>`, `<use>`, etc. are not included.
2. Read absolute/relative SVG path commands (M, L, H, V, C, S, Q, T, A, Z). For curves and arcs, retain their *endpoint* only. Each subpath with at least three distinct vertices defines a filled polygon; open contours are implicitly closed for the fill.
3. Assemble non-intersecting contours according to inherited SVG `fill-rule` (nonzero/evenodd), including holes/islands.
4. Optionally subdivide boundary segments without changing the boundary curve (which is now a set of straight chords).
5. For a hole-free polygon, advance the polygon front using Mesher's 75°/135° 1-/2-/3-triangle modes, insertion factors 0.7, 1.0 and 0.45, and net-front-reduction preference. Check trial polygons and triangles for overlap, inversion, out-of-bounds area, and conservation of covered area.
6. For a polygon with holes, or an advancing-front dead end, fall back to GEOS constrained Delaunay triangulation for that entire polygon or remaining front. This guarantees an exact constrained closure but will not have the same advancing-front triangle style in that portion.
7. Verify the union of all output triangles covers precisely the original straight-edged filled area, with no overlapping triangle interiors, within a scale-relative floating-point tolerance. Error out if checks fail.
8. Insert mesh-edge paths and vertex circles in a new group immediately following each source path. Nested parent transforms and path-local transforms are retained. Existing generated groups are stripped on rerun to avoid remeshing mesh edges.

### Limitations

- Contours must be valid, non-self-intersecting polygon rings; different contours of the *same path* must be nested or separate, not mutually intersecting/touching. Invalid geometry is rejected instead of outputting an incomplete mesh.
- Geometry defined by CSS stylesheets, `<use>`, masks and clipping arrangements is not expanded. The path's actual vertices are used; group and path transforms are carried into the output SVG without resolving their coordinates numerically.
- Rendering stroke width adds pixels outside the mathematical triangle boundary. The **centerlines of mesh edges** match the polygon boundary exactly.
- This is a 2D adaptation, not a verbatim implementation of the 3D geometric placement, mirrored seam or fixed-rim Poisson relaxation.

## Regression tests

```bash
python -m unittest -v test_mesher_svg_front.py
```

They verify desired behavior: full coverage, nonoverlap, hole winding, concavity, boundary splitting, curve/arc endpoints and advancing-front operation.
