# STL facet RGB export: dual-graph error diffusion

## Accepted behaviour

VisCAM/SolidView and Materialise Magics **facet RGB** exports retain their
original file-dialog filter labels and shared warning:

> Colourized STL is based on facet colours instead of vertex colours. Mesher
> makes a best effort to distribute vertex colours across the faces of the mesh,
> but the exported file is unavoidably not an exact representation of the Mesher
> document.

Their colour conversion now **always** uses source-palette selection with
bounded error diffusion on the mesh's topological dual. The earlier
three-vertex RGB averaging was retired because it blurred high-frequency
dithered or quantized vertex-colour patterns. The optional palette-only and
mean comparison filters have also been retired, returning the export menu to
its original 21 entries (4 OBJ, 12 PLY, 5 STL).

Magics **single-object RGBA** is unrelated: that format stores one global
colour, so it deliberately retains its separately disclosed whole-object
RGBA averaging policy. ASCII and geometry-only binary STL are unchanged.

## Dual-graph colour conversion

A triangle in the original mesh becomes a node in the dual graph. Two dual
nodes are adjacent if their source triangles share an edge. Polygonal dual
faces need not be constructed.

For each triangle, the three source vertex RGB values are individually
quantized to 5:5:5, providing up to three representative candidates.
The continuous RGB mean is **only an internal error target**, never an
exported facet colour. A deterministic seeded minimum-remaining-degree
traversal chooses one available source candidate, biased by incoming
one-hop and two-hop residuals. Residuals propagate across still-unprocessed
dual edges with weights based on shared-edge length, face area, spatial
distance and source-colour similarity. Bounded area compensation prevents
large differences in triangle size from dominating neighbours.

The result is encoded into the existing R-high VisCAM or R-low Magics
binary facet attribute field. The writer performs validation and computes
the complete array of facet colours *before writing any bytes*, so failure
cannot publish a partial output. The asynchronous exporter's atomic
temporary-file publication policy remains unchanged.

## Acceptance and limitations

Physical acceptance compared source vertex-colour imagery and STL output
in MeshLab2 on a large dithered image (279,482 vertices, 558,960 faces).
The facet output preserved the deliberate black/white dithering more closely
than the former averaging implementation. Additional colour variants
(8-colour quantized, original colour image and undithered black/white)
round-tripped well visually. An STL export followed by a new facet-to-vertex
diffusion import does **not** guarantee preservation of the original spatial
energy distribution; geometric patterns can emerge in mid-density regions.

The regression `stl_dual_facet_colour_diffusion` verifies a closed,
high-frequency black/white mesh yields **only source colours**, not
intermediate greys, and checks deterministic behaviour, uniformly coloured
meshes, RGB555 bit packing for both dialects, and failure without partial
output. Existing colour/geometry/dialect round-trip tests remain in place.

`StlFacetColourMetrics` still exposes preparation time, assignment time,
area-weighted RGB error against the continuous target, global RGB bias and
edge-length-weighted colour contrast for diagnostics. Lower per-facet RGB
RMS is not the objective of palette-preserving diffusion; the former mean
was optimal for that different metric.
