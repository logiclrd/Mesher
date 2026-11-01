# STL facet RGB: dual-graph palette and diffusion experiment

## Goal

Preserve high-frequency per-vertex colour structure when converting a Mesher
mesh to 5-bit-per-channel facet-coloured STL. A Floyd-Steinberg-dithered source
can become visibly blurred if every triangle's three vertex colours are
averaged. An area-weighted arithmetic mean is optimal for per-triangle squared
error, but is not necessarily the best way to preserve local contrast or a
source colour palette.

The existing **Mean** export remains the default and its byte format is
unchanged. The two experimental alternatives do not affect imported STL,
Magics object RGBA, or uncoloured exports.

## Select the export mode using Ctrl+E

For each of the VisCAM/SolidView and Materialise Magics dialects:

- **facet RGB** (original filter): mean each triangle's vertex RGB, then
  quantize to RGB555.
- **facet RGB - vertex palette**: select the nearest of the triangle's three
  vertex RGB colours, **after RGB555 quantization**; do not introduce blends.
- **facet RGB - dual diffusion**: use the same three quantized source candidates
  but bias each face's selection using incoming colour residuals from
  previously processed adjacent triangles.

Both experimental modes still lose alpha and are approximate, and the
confirmation dialogue warns accordingly. There are now 25 export choices:
4 OBJ, 12 PLY, and 9 STL.

## Dual diffusion

A primal triangle becomes a node of the dual adjacency graph. Nodes are
adjacent when the corresponding primal faces share an edge. We never need to
construct polygonal dual faces.

Each node starts with the arithmetic mean of its source vertex RGB colours.
Candidates are the triangle's three source vertex colours, quantized to RGB555.
A deterministic seeded minimum-remaining-degree traversal assigns one candidate
per node. The target includes bounded one- and two-hop incoming RGB residuals.
The chosen-colour residual propagates over still-unprocessed dual edges,
weighted by shared-edge length, neighbouring area, spatial distance and source
colour similarity. Area compensation is bounded to prevent extreme rescaling.

The baseline and vertex-palette strategies skip dual adjacency construction.
The exporter validates topology and colour data and calculates ALL attributes
before writing any STL bytes. The asynchronous export's atomic temporary-file
publication policy remains unchanged.

## Evaluation

The `stl_dual_facet_palette_and_diffusion` test makes a closed high-frequency
black/white ellipsoid and checks:

- the default mean produces intermediate grey facets, while both experimental
  strategies only emit the two available source colours;
- seeded diffusion is deterministic and actually differs from independent
  palette selection;
- both Materialise Magics and VisCAM RGB555 byte layouts match the selected
  strategy, with no geometry or facet-count changes;
- uniform colours remain exact, non-facet modes reject inappropriate options,
  and metrics remain finite.

The C++ API `stlFacetColourRGB555` exposes optional
`StlFacetColourMetrics`: graph-preparation time, assignment time, RGB RMS versus
continuous facet means, area-weighted RGB mean bias, and edge-length-weighted
facet contrast. **RGB RMS is expected to favour averaging mathematically**;
larger contrast is not necessarily better either. Judge photographic detail,
high-frequency colour frequencies, local contrast, and runtime together.

Physical A/B/C acceptance: export the same dithered matrix STL source from
Mesher using the three Materialise Magics facet RGB filters, then inspect
the results in MeshLab2 at the same camera, illumination and scale. Repeat
on Duck and castle. Do not replace the historic baseline until the alternatives
have been physically compared.
