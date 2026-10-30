# UX06D — STL facet-to-vertex colour conversion

Status: **contrast-aware graph error diffusion is the production default**
for coloured STL import, following visual comparison of the same castle STL
with all three algorithms. The choice applies to the current *vertex-colour
approximation*, not to a final decision about the mesh attribute architecture.
PR #179 remains draft until CI and physical acceptance are complete.

## Problem and invariants

STL binary colour extensions may assign independent, constant RGBA colours
to adjacent facets sharing exact geometric vertices. Mesher currently carries
editable interpolated *vertex* colours. Enforcing lossless correspondence
refuses real coloured STL meshes; a simple all-neighbour average passes sharp
colour boundaries through a low-pass filter. We will import these meshes
approximately **without** changing topology or inventing extra welded vertices.
Unknown attribute encodings, invalid geometry, unsupported MATERIAL= metadata,
and broken/wrong winding continue to refuse. Ordinary uncoloured STL remains
white and does not run a colour experiment.

For mixed coloured/uncoloured VisCAM facets, use opaque white as an explicitly
documented fallback for uncoloured facets, not an invented source colour.
Keep Magics object RGBA (including its documented alpha) as specified.

## Colour conversion algorithms

1. **Area-weighted average**: incident-face RGBA, weighted by face area / 3.
   Baseline representing conventional smoothing. No random decisions.
2. **Stochastic incident face**: choose one incident facet's exact colour by
   area-weighted deterministic pseudo-random sampling. No forced averaging;
   ties/seed produce repeatable results. Compare preservation of sharp source
   palettes against the possible speckle/noise.
3. **Contrast-aware graph error diffusion**: traverse vertices using a
   Warnsdorff-like smallest-unvisited-degree priority order with seeded,
   deterministic tie-breaking. Candidates are incident source face colours
   and a bounded set of pairwise blends. Score each assignment using the
   *rendered face centroid* error for all incident triangles, with already
   chosen vertex colours and incident-area averages as provisional values
   for unvisited vertices. Apply accumulated local quantization error; pass
   it to unprocessed graph neighbours, weighted by surface distance,
   neighbourhood area and source-colour similarity. Limit propagation to
   at most two graph edges and attenuate energy on each hop. Preserve
   topology and exact source colours for comparison.

We are **not** promising that an adapted vertex-quantization method is
identical to published mesh error-diffusion methods: transferring constant
facet colours to interpolated vertices is a different optimization problem.
All algorithms receive the *same* welded mesh, exact decoded facet colours,
and fixed seed; none is allowed to modify the geometry.

## Production selection and diagnostic controls

Normal Ctrl+O of a coloured STL executes **only diffusion** (the default).
This avoids spending import time running the two algorithms which lost the
visual comparison. Keep `--stl-colour-method average|stochastic|diffusion`
and `--stl-colour-seed N` for explicit deterministic diagnostics: choosing
`average` or `stochastic` executes that selected conversion only. The
dedicated `stl_colour_benchmark` CTest still computes and profiles all three
candidates against the same geometry and decoded facet colours.

Normal imported-colour diagnostics report selected method, seed, graph setup
time, that method's conversion time, evaluation time and colour-fidelity
metrics. Only the explicit benchmark prints side-by-side metrics for all
three. This work is import-time only; no sculpt hot-path behaviour or other
mesh import format changes.

## Visual acceptance and measurements

The user's three screenshots of the regenerated image-based castle STL
established **diffusion** as the preferred reconstruction: area averaging
visibly blurred image details, while stochastic selection introduced
high-frequency jagged noise; diffusion retained detail with much less
objectionable speckling. This is an empirical visual preference for this
representative fixture, **not** a proof that diffusion minimizes error for
every source mesh. The two alternatives remain available for future
comparisons.

## Measurements

- Face-area-weighted RGB RMS **integrated across rendered triangles**
  (using barycentric interpolation, not only centroids).
- Face-area-weighted alpha RMS reported separately; retain RGBA provenance.
- Area/edge-length-weighted *adjacent-facet contrast error* comparing source
  colour difference and reconstructed centroid colour difference.
- Performance for the same mesh: preprocessing, candidate wall times,
  evaluation times, triangle/vertex counts, seed and selected method.

The integrated error for a face with vertex colours `v1,v2,v3`, constant
target `f` and mean `m=(v1+v2+v3)/3` is
`||m-f||² + (||v1-m||²+||v2-m||²+||v3-m||²)/12` per unit area.
Contrast is reported independently because low RMS by itself can reward
blurring boundaries. These metrics do not stand in for screenshots.

## Regression/acceptance

- Coloured tetrahedron with all equal facet colours: every candidate exactly
  preserves colour; ordinary STL geometry and transparent Magics defaults
  still work.
- Conflicting adjacent facet colours and mixed coloured/uncoloured VisCAM:
  all three produce valid vertex colours and import successfully; no
  historical red test accepting refusal as desired behaviour.
- Determinism for each method, different seeds allowed to vary stochastic
  results, all values finite and clamped [0,1].
- Real error measures and nonnegative finite times for the *selected*
  production conversion; explicit benchmark still prints all three.
  Verify default output matches an explicit diffusion selection.
- Original uncoloured STL bypasses the candidate computation; invalid
  topology/unknown format still refuses and leaves the current editor intact.
- The real castle STL has been compared visually with all three methods;
  diffusion is the accepted production selection for this attribute model.
  Complete exact-head five-platform CI and packaging, and retain PR #179
  unmerged until the user explicitly authorizes merging it.

## Limitations to investigate

A continuous interpolated vertex stream cannot exactly reproduce independent
per-facet discontinuities on shared vertices. Even graph diffusion may show
salt-and-pepper texture or inaccurate flat regions. A future per-face or
per-corner colour attribute (with render-only vertex duplication) is the
lossless alternative and remains a separate architectural decision. Diffusion
is the current visually preferred conversion, not a lossless replacement for
per-facet or per-corner attributes.
