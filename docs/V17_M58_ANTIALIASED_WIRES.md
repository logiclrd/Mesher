# V17-M58 — Physically visible antialiased depth-correct mesh and grid edges

## Source evidence and requirement

In the real Linux Vulkan screenshot after M56, hidden wire segments
were correctly occluded by nearer solids, but the displayed mesh
edges and grid lines were visibly stippled and thinner than a pixel
at diagonal angles. The common source was the Vulkan wire pipeline:

* `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` of two-vertex edges;
* `raster.lineWidth=1`, `VK_SAMPLE_COUNT_1_BIT`, no MSAA or
  smooth coverage.

A previous global clip-Z pull made these lines appear stronger
but incorrectly exposed rear faces. M58 must NOT restore this bias.

## Implementation

The real CPU indexed partition renderer still stages exactly **one
unique two-endpoint segment per live topological edge**. Stable
`VertexId`/`FaceId`, immutable `MeshRevision`, workers, source
certificates, reflections, first-original provenance, and work
limits do not change. At the actual Vulkan upload or
dirty-partition replacement **only**, `expandWireQuads` turns
each two-endpoint segment into two triangles (six vertices).
The same conversion is used for legacy flat-mesh wires and the
M57 dedicated floor grid. A bounded expansion of one
dirty partition never traverses the whole mesh.

The established seven-float `PartitionRenderVertex` binding is
reused in a *dedicated* wire GPU upload format: position=A,
normal.xyz=B, material=corner index 0..3, using the deterministic
triangle-corner order `{0,1,2,0,2,3}`. Original CPU staging is
unchanged, so M54 independent geometry audit continues to compare
real current topology edges against unmodified renderer data.

The wire vertex shader projects both original endpoints using
the same model/view/projection matrix as solid triangles, clips
near-plane-crossing endpoints before screen-space normalization,
rejects zero-length/degenerated projections, and offsets **only
projected X/Y** by a view-perpendicular halfwidth of two physical
framebuffer pixels. Push constants carry actual framebuffer
width/height per frame, including HiDPI and window resize.

The wire fragment shader receives **noperspective** pixel-space
distance and produces a one-pixel smooth alpha coverage ramp from
opaque center to transparent edge. The Vulkan wire pipeline
uses triangle fill, normal alpha blending, remains
`VK_COMPARE_OP_LESS_OR_EQUAL` depth-tested with **no depth
writes**, and is drawn after the solid depth-writing geometry.
The corrected M56 Z-occlusion behavior is retained: **no
clip-space depth bias** is applied. The nominal appearance is a
visibly continuous dark stroke, not the former aliased one-pixel
LINE_LIST.

M56 `W` continues to hide both mesh and grid wire overlays
without hiding solids. M57 `G` independently hides both scenic
floor solids and lines without affecting the COW mesh. Orbit,
dolly, picking, undo/redo and non-viewport exports are unchanged.

## Tests and remaining gates

`tests/wire_quads_tests.cpp` directly validates the six real
GPU vertex corners and source endpoint encoding on simple,
asymmetric and reflected COW fixtures and all 34 floor grid
segments; an odd number of edge vertices refuses before upload,
and source arrays are unchanged.

`tests/verify_aa_wire_quads.cmake` checks source-derived
GLSL pixel-AA and near clip guards, real Vulkan triangle
topology, full seven-float wire attributes, blended coverage,
identical wire depth test/read-only state, finite 2px
halfwidth push constants, **all** legacy/grid/COW
upload and dirty-replacement paths, and absence of the
old clip-Z bias. M56's original depth-occlusion CTest is
extended to both projected source endpoints.
The existing M54 accepted-union manifold/wire-root proof
and complete GCC, Clang and Windows MSVC /W4 /WX
Vulkan-enabled test/stress/benchmark matrix must also pass
on exact final PR head and independently merged `main`.

The M57 merged-main GCC test failure was diagnosed as an
old test-observer race: a worker completion counter could
advance before the asynchronous renderer snapshot became
visible. A bounded exact-root+history wait replaces the
incorrect immediate snapshot assertion in the existing
mirrored-worker regression; production code is untouched.

**This is a compiled and mathematically tested GPU path,
not a claim of human-observed appearance on all drivers.**
M52 must still inspect real X11/Wayland and Windows Vulkan
rendering at representative DPI, orbits, wire on/off, grid
and signed source-positive Merge. Original M15 remains open.

## Reconciliation and automated acceptance (October 20, 2026)

PR [#111](https://github.com/logiclrd/Mesher/pull/111) was updated to
include latest M59 merged `main` and the user's independent console
newline correction. The M58 wire expansion now preserves M59's
intentional empty indexed-COW legacy upload/draw guards, while AA
wires are uploaded for nonempty legacy, floor and partition data.
The M57 floor and M59 optional-buffer source-contract tests were
updated to assert the *combined* renderer rather than obsolete
LINE_LIST-era calls. The source-owned CPU fixture test iterates
meshes by const reference to satisfy GCC/Clang `-Werror`; the
mirrored-worker exact-root/history observer fix remains test-only.

The exact integrated implementation at `f7308822` passed
[GitHub Actions #39491852308](https://github.com/logiclrd/Mesher/actions/runs/39491852308)
on Vulkan-enabled Linux GCC, Linux Clang and Windows MSVC /W4 /WX.
GCC registered and passed **all 39 CTests**, including both M58
AA tests and M59's optional-buffer startup guard. Each platform
passed its full workflow, including threaded checkpoint/history
stress and applicable benchmarks. This proves compiled contract
and CPU correctness on the supported fixtures; **not** actual
rendered line appearance on a physical Vulkan device. An independent
merged-`main` CI run is the final integration gate, and M52's
human-observed Linux/Windows graphics acceptance remains open.
