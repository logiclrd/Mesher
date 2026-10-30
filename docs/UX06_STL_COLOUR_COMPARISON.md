# UX06D — Facet-to-vertex colour conversion experiment

Status: **approved experiment**, not a choice of final attribute architecture.
Requested following STL parser PR #179; remain on its draft feature branch
until CPU comparisons, screenshots and user review are complete.

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

## Three competing algorithms

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

## Experimental controls

On Ctrl+O, compute all three candidates on the private mesh and log their
individual compute times and colour fidelity metrics before selecting one
for display. Add `--stl-colour-method average|stochastic|diffusion` (default:
**diffusion provisionally**, not an architectural decision) and
`--stl-colour-seed N` for exact repeatability. Restart/open the same STL with
different options for visual A/B/C. No other mesh import format is changed.
Avoid a modal dialog until we have compared the algorithms.

Log separate graph setup/preprocessing time, per-method conversion time,
per-method evaluation time and overall parse/validation time. Keep
instrumentation off the sculpt hot path. Make logs easy to copy for a later
side-by-side comparison.

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
- Real error measures, nonnegative finite times, print machine-usable
  consistent summary lines; compare selected output to selected candidate.
- Original uncoloured STL bypasses the candidate computation; invalid
  topology/unknown format still refuses and leaves the current editor intact.
- Run five-platform CI and packaging, plus physical colour comparisons on a
  coloured STL on Windows or Linux. **Do not merge PR #179 until reviewed.**

## Limitations to investigate

A continuous interpolated vertex stream cannot exactly reproduce independent
per-facet discontinuities on shared vertices. Even graph diffusion may show
salt-and-pepper texture or inaccurate flat regions. A future per-face or
per-corner colour attribute (with render-only vertex duplication) is the
lossless alternative and remains a separate architectural decision. We will
compare, not presume that this experiment beats such an attribute design.
