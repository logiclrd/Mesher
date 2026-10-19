# Mesher design principles (future milestones)

> **Planning authority for ongoing V17:** [flat V17-M01–M16 milestone plan](docs/V17_MILESTONES.md) (rebaseline after PR #50, with append-only M16 coincidence gate added 2026-10-18); [normative coincident-geometry/identity contract](docs/COINCIDENT_VERTEX_TOPOLOGY.md); [original nested V17 tasks, verbatim](docs/V17_LEGACY_TASK_TREE.md). The sections below remain a chronological technical record, not active recursively numbered TODOs.

The current milestone includes a generated manifold, Vulkan viewport,
and a CPU Inflate-brush stroke engine. This document distinguishes implemented
brush behaviour from future geometry-editing capabilities.

## Implemented brush-stroke contract

- Stroke begins with one or more spherical influences (center, radius, hardness).
  Influence is the **maximum** of all spheres at a vertex, not their sum.
  Falloff is 1 inside the hardness fraction of the radius and otherwise
  decreases to 0 via a smooth S-curve (quintic smootherstep).
- All centers move the same distance per stroke update. The stroke samples
  them together at a global fixed arc-length Step, preserving fractional
  progress across updates and corners. It applies the initial stamp and
  guarantees the final endpoint is sampled without duplication.
- Inflate moves vertices along the normalized, equally weighted sum of all
  incident face unit normals, recomputed for each stamp from pre-stamp geometry.
- The demo applies mirror-image half-circles on the front of the sphere.
  Remeshing, density maintenance and interactive painting are **deferred**.

## Non-destructive, minimally invasive topology

- Ordinary brushes first **refine only as necessary**, so the current geometry
  has enough degrees of freedom to represent the requested deformation or
  paint stroke. A brush's editing density is a **minimum**, not a density to
  which an already-fine mesh should be reduced.
- For ordinary deformation, preserve all existing topology wherever possible.
  No automatic decimation merely because a coarse brush passed over detailed
  mesh data.
- Make a standalone brush for explicitly painting local mesh density, with
  refine/simplify/normalize modes. Refinement of planar triangles should not
  move the old surface (no implicit Catmull–Clark smoothing).
- During a stroke, subdivide an overlong edge only when the deformation is
  stretching it farther, and consider collapsing an undersized edge only when
  deformation is compressing it further. Measure actual **local deformation**
  rather than classifying entire brush types as inflate or deflate.
- Collapse candidates must additionally satisfy geometric **and vertex-colour**
  error budgets. A length threshold permits evaluation; it never mandates a
  destructive collapse.
- Preserve manifold connectivity and validity even when artist-controlled
  density policy permits stretching or bunching. Hysteresis must avoid
  split/collapse oscillation.

## Appearance

- Vertex colours are first-class; geometry and colour sample density are tied.
- Importing textured meshes should eventually offer texture-aware local
  tessellation that approximates the texture's appearance to an explicit error
  tolerance without unnecessary global subdivision. Exact finite triangle
  representation of arbitrary bilinear textures is not always possible.
- Never decimate away important scanned geometry or colour details simply to
  reach a requested polygon count.

## Performance and reliability

- CPU topology structures are separate from GPU draw buffers; future updates
  should upload **only changed regions**.
- Editing operations should commit consistent topological transactions;
  failed operations must not leave a half-modified mesh.
- Use a local spatial index for brush hit-testing, deformation, remeshing, and
  validation rather than visiting every vertex in large meshes.
- Explore chunked copy-on-write history and asynchronous crash-recovery
  checkpoints, explicitly trading persistence latency against recovery
  guarantees. Avoid requiring a full-mesh copy per brush stroke.
- Regression tests should cover boundary, degenerate, self-intersection,
  seam, and topology-invariant cases as the feature set grows.

## Open implementation decisions

- Replace the baseline incremental convex hull with a scalable triangulation
  algorithm before importing meshes with millions of vertices.
- Select a mutable half-edge / corner-based topology representation with stable
  vertex IDs and bounded local update costs.
- Define geometric and colour error metrics, surface projection, high-resolution
  persistence, and live brush input/picking policy. Stroke sampling/falloff
  and the first Inflate brush are already implemented.
- Add camera navigation, mesh interaction, editing tools, and import/export.
## Live Inflate deformation and diagnostic playback

The symmetric generator shares equatorial vertex **indices**; the observed
crossing wires are not caused by separate hemisphere vertex rings. Repeated
normal-based deformation can rotate incident triangle normals until triangles
fold through the symmetry plane, despite the original normals pointing outward.

We reverted the temporary stroke-start frozen-normal workaround so the Inflate
brush once again uses the current geometry's face normals **every stamp**.
The symmetric half-circle demo is now processed one stamp per viewport update
at 10 stamps/second (configurable). `InflateDemoAnimation` records a sequence
using normal `BrushStroke` sampling, then applies each stamp incrementally.
It supports reset and exact replay. The rendering layer owns the animation
clock and updates the same Vulkan buffers only after waiting for the prior GPU
frame to finish. This is full-buffer replacement for now, not dirty regions.

The viewport reports the number of *radially inverted* triangles to help
locate the start of a fold. This is only a diagnostic for the unit sphere,
not a complete test for self-intersection. Geometry fold prevention and
remeshing remain intentionally deferred.

## Directional collapse / pinch experiment

Before and after each individual Inflate stamp, compare lengths of the same
edges. Candidates satisfy `L_after < ratio * h_initial` AND
`L_after < L_before` (with a small numeric tolerance). Process shortest first
with a bounded count per stamp. Apply mirrored operations atomically using
the sphere generator's known vertex correspondence, preserving the single
equatorial ring. Reject candidates violating the two-manifold edge-link
condition, consistent closed-sphere topology or a previously positive face's
outward winding. A valid collapse may still change surface shape and delete
vertex-colour detail in future versions. This experiment deliberately uses
midpoint positions to test the low-pass intuition; it does not yet preserve
fine geometric or colour details.

Track minimum edge length, minimum triangle altitude, inward/inverted radial
faces, shrinking short edges and accepted edge collapses per stamp. The
`--compare-remesh` diagnostic runs the identical stamp trajectory with
and without collapse and emits CSV data; `--collapse` enables the same pass
in the Vulkan animation. This does not claim complete self-intersection
prevention. For future meshes the remeshing API must be extended to preserve
per-vertex attributes and handle arbitrary symmetry planes / imported meshes.

## Measured 5,000-vertex regression (2026-10-15)

Linux CI runs the reproducible `--compare-remesh` experiment with the default
5,000-vertex unit sphere, two radius-0.4 mirrored Inflate influences, 105
stamps, default minimum-edge ratio 0.65 and up to 12 symmetric collapse
operations per stamp:

| Measurement | No collapse | Directional collapse |
| --- | ---: | ---: |
| First shrinking edge below threshold | 11 | 11 |
| First radially inverted face | 98 | 105 |
| Final inverted faces | 22 | 8 |
| Final vertices | 5,000 | 4,898 |
| Minimum edge length | 0.0176079 | 0.0305529 |
| Minimum triangle altitude | 0.00323476 | 0.0117284 |

There is improvement, **not a guarantee of fold-free geometry**. The current
conservative constraints preserve closed, oriented mesh connectivity but do
not detect geometric self-intersections or forbid already existing folds.
The observations support investigating compression-triggered refinement and
collapse before adding curvature-aware movement or collision constraints.


## Independent curvature-correction experiment

`--curvature-correction` enables a separate local correction to Inflate's
normal-based displacement; `--curvature-strength GAIN` enables it and selects
a nonnegative gain (default **1.0**). It can be used alone or together with
`--collapse`. Without either flag the original Inflate behaviour is unchanged.

For each active vertex, compute the standard cotangent-weighted
Laplace-Beltrami vector `L` from the **pre-stamp** triangles, and the current
averaged outward normal `n`. The signed normal curvature proxy is
`k = dot(L, n)`, which is negative on a convex sphere and positive in a
concave valley. With `h` the vertex's mean incident edge length, use

```text
multiplier = clamp(1 + GAIN * h * k, 0.10, 3.0)
displacement = normal * normalInflateStrength * influence * multiplier
```

This is a small, explicit, **normal-only** curvature-flow correction to
the existing Inflate law. The multiplier is bounded to avoid extreme
velocities when triangles become very thin; cotangent weights are also
clamped. It is not a full mean-curvature-flow solver, no tangential smoothing
is performed, and no guarantee against foldovers is claimed. Like the
existing brush, the correction is evaluated from the mesh at each stamp,
so it remains compatible with topology changes from the optional collapser.

For a four-way, same-path comparison, run:

```sh
./build/Mesher --compare-mitigations > fourway.csv
./build/Mesher --compare-mitigations --curvature-strength 2.0 > stronger.csv
```

The CSV includes **baseline**, **curvature only**, **collapse only**, and
**both**, with one measurement row per stamp for each. The ordinary two-way
`--compare-remesh` remains available. CI runs the four-way experiment
at gains 0.5, 1.0 and 2.0 and uploads the comparison CSV files as artifacts.

In the viewport, try `./build/Mesher --curvature-correction` or
`./build/Mesher --collapse --curvature-correction` for direct visual
comparison at 10 stamps/s. Use Space/Right/R to pause, single-step and replay.

## Measured 5,000-vertex curvature experiment (2026-10-15)

Four-way independent comparison of the original 105-stamp, radius-0.4,
5,000-vertex symmetric Inflate test:

| Options | First radially inverted face | Final inverted faces | Final vertices |
| --- | ---: | ---: | ---: |
| Neither | 98 | 22 | 5,000 |
| Curvature gain 1.0 only | Never | 0 | 5,000 |
| Directional collapse only | 105 | 8 | 4,898 |
| Both, curvature gain 1.0 | Never | 0 | 4,944 |

Additional strength sweep:

| Curvature gain | Curvature only: first inversion / final | Both: first inversion / final |
| --- | --- | --- |
| 0.5 | Never / 0 | Never / 0 |
| 1.0 | Never / 0 | Never / 0 |
| 2.0 | 98 / 0 | Never / 0 |

The **2.0 curvature-only** run has a transient inversion. This illustrates
why checking only the final deformation would give a misleading assessment.

The correction is experimentally promising, but radial inversion counts
do not establish absence of other types of self-intersection. We have not
tested imported meshes or noisy photogrammetry scans; the curvature term
may also attenuate legitimate high-frequency geometric detail. It should
remain **opt-in** while we evaluate fidelity and stability. The default
unmodified Inflate remains available for A/B comparisons.

See GitHub Actions `mesher-curvature-experiments` artifacts for complete
per-stamp CSV data.

## Window-manager fullscreen and performance HUD

Mesher now starts as a **normal window-manager-managed window** rather than
calling GLFW with the primary-monitor handle. On X11 (KDE Plasma, KWin,
and other EWMH-compatible window managers), the window is first mapped normally
and then requests `_NET_WM_STATE_FULLSCREEN` from the window manager. The WM
determines placement according to its rules: follow mouse, fixed monitor, and
other user preferences. The window does not change display modes or own the
monitor. When exiting with Escape, Mesher asks the WM to reactivate the window
that was focused before launch (typically the launching terminal); the WM is
free to accept or reject that focus request according to its policy.

On builds without X11 integration, GLFW uses a managed **maximized window**
as a fallback. This may leave panels visible; the portable GLFW API cannot
request compositor-selected true fullscreen on every backend, especially
Wayland. In all cases the initial window is not pinned to the primary monitor.

A built-in 5x7 bitmap-font GPU overlay in the **top-right corner** shows
`FPS` (frames submitted/presented per second) and
`STAMPS` (actual completed mesh updates per second / requested rate).
Both are measured over approximately 0.75-second wall-clock intervals,
not assumed from the configured timer. FPS can be limited by vsync,
presentation or the GPU; stamp throughput also includes CPU remeshing cost.
The two rates are deliberately measured independently. The stamp schedule
is aligned to absolute deadlines rather than restarting its interval after
each update, with at most one stamp between visible frames; no brush stamps
are discarded.

The Vulkan HUD is screen-space, depth-independent, and includes a translucent
dark backing. It requires no external font assets and adapts to swapchain
resize/fullscreen transitions.

Actual window-manager placement and focus restoration require testing under
the user's desktop session; CI compiles the complete Vulkan app and shaders,
but does not have a running KWin compositor to check these desktop behaviours.


## Experimental stretch-driven local refinement

Use `--refine` to enable **directional subdivision plus local edge flipping**;
the existing collapse pass (`--collapse`) and curvature correction
(`--curvature-strength 0.25`) remain independent options. For example:

```sh
./build/Mesher --refine --curvature-strength 0.25
./build/Mesher --refine --collapse --curvature-strength 0.25
./build/Mesher --refine --no-edge-flips --curvature-strength 0.25
```

A split requires **both** `edge_length_after > maximumEdgeRatio * h_initial`
and `edge_length_after > edge_length_before`. The default maximum-edge
ratio is **1.5**, with a budget of **8 split-pairs per stamp**. This never
converts an already-fine patch to a coarser target. Both triangles sharing an
edge are subdivided, maintaining a conforming manifold mesh. In symmetric
mode, a split and its reflected counterpart occur together, sharing a single
new equatorial midpoint when the edge itself lies on the seam. Newly inserted
positions and the corresponding pre-stamp reference positions are both
interpolated from the original edge endpoints. In the polygonal surface
representation, midpoint insertion alone changes neither existing geometry
nor vertex-colour interpolation.

After splits, a bounded one-ring neighbourhood may be reorganized with
edge flips (**12 paired flips max per stamp**, at most eight attempts per
allowed flip). A flip must improve the minimum triangle shape quality or
reduce vertex-valence irregularity without meaningful triangle-quality loss.
We reject changes to sharply nonplanar pairs, already-inverted faces and
nonmanifold connectivity; reflected flips are paired. The two stages are
separately controllable using `--no-edge-flips`,
`--maximum-edge-ratio R`, `--max-split-pairs N`, and
`--max-flip-pairs N`. When directional collapsing is enabled, it runs
**after** refinement and receives the updated per-vertex pre-stamp reference
positions and reflected-index mapping.

For the same deterministic 105-stamp trajectory, compare variants without
the GPU window using:

```sh
./build/Mesher --compare-local-remesh --curvature-strength 0.25 > local.csv
```

This reports `baseline`, `collapse`, `split`, `split_flip`, and `all`
with per-stamp triangle/vertex counts, minimum altitude, inversion count,
splits, flips, and collapses. It does **not** assert that topology
optimization must always improve the final shape; visual comparison remains
essential. Legacy `--compare-remesh` and `--compare-mitigations` retain
their original baseline semantics.

**Current limitations:** the operation budgets and local neighbourhood
bounds are deliberate, not globally optimal; the implementation scans the
whole small demo mesh and is not yet suitable for huge scans. Edge flips
approximate the same piecewise-linear surface and thus can alter geometry
or colours, unlike pure edge splits. The normal-compatibility gate is only
a coarse fidelity filter: a quantitative geometric and colour-error budget
is needed before general scan editing. There's no global collision guarantee,
triangle projection/reconstruction, or smooth 2:1 gradation enforcement.


## Local split/flip measurements (2026-10-15)

The five-way `--compare-local-remesh --curvature-strength 0.25` regression
uses the *same* 105 stamps for all modes and caps splitting, flipping and
collapsing at **three paired operations of each type per stamp**. The
numerical results demonstrate local topology reorganization, **not**
necessarily improved visible surface morphology.

At **5,000 vertices** initially:

| Mode | First radial inversion | Final inverted | Added | Flips | Removed | Final vertices |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 102 | 0 | 0 | 0 | 0 | 5000 |
| Collapse | Never | 0 | 0 | 0 | 88 | 4912 |
| Split | 102 | 0 | 612 | 0 | 0 | 5612 |
| Split + flip | 102 | 0 | 609 | 172 | 0 | 5609 |
| All | Never | 0 | 610 | 103 | 136 | 5474 |

At **1,500 vertices** initially (same operation caps):

| Mode | Added | Flips | Removed | Final vertices |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 0 | 0 | 0 | 1500 |
| Collapse | 0 | 0 | 24 | 1476 |
| Split | 292 | 0 | 0 | 1792 |
| Split + flip | 218 | 104 | 0 | 1718 |
| All | 263 | 82 | 60 | 1703 |

No inversions occurred in any 1,500-vertex configuration. At the full
5,000-vertex resolution, **splitting and flipping alone did not eliminate
the transient inversion at stamp 102**; collapse did, both alone and
in combination with split/flip. Edge flips reduced subsequent split
requirements considerably in the smaller test, but only slightly at 5,000.
This reinforces the need to judge geometry and fidelity in addition to
polygon counts.

Local refinement is **opt-in** and currently post-stamp only. Production
sculpting will also need pre-stamp refinement where the original triangle
resolution is inadequate to represent the brush. The geometric and
vertex-colour fidelity checks on edge flips must be strengthened before
using this on photogrammetry data.


## Mouse sculpting and 4D stroke spacing

**Default viewport mode is now interactive.** Left-click the sphere to begin
an Inflate stroke, hold stationary to keep inflating, drag across the changing
surface to sculpt, and release to finish. Escape exits. The viewport raycasts
against the **latest deformed mesh** each frame (not an original reference
sphere); dragging outside the silhouette retains the last valid brush center
until the pointer returns. Floor/grid triangles are excluded from picking.

The brush stroke sampler treats each influence center as a four-dimensional
point: `(x, y, z, timeSpeed * elapsedSeconds)`. For any update, the travel
is the 4D Euclidean distance, and existing **Step**, sample interpolation,
phase/overshoot, equal-distance-between-influence-points, initial stamp,
and endpoint semantics remain the same. The time coordinate increases
monotonically; a stationary mouse therefore emits stamps at
`timeSpeed / Step`. Default `timeSpeed = 0.2` model-units/second and
`Step = 0.02` give **10 stationary stamps per second**. Motion makes the
4D trajectory longer, increasing stamp frequency naturally. Use
`--time-speed N` to adjust stationary speed.

With the default **symmetric sphere**, each mouse point is paired with
its exact YZ-plane reflection, and the brush uses the maximum influence
of the two spheres (never doubles overlapping influences). With
`--asymmetric`, no second influence is created and no mirror mapping
is passed to topology maintenance. This distinction is determined by the
generator, not the cursor position. Symmetric topology maintenance remains
available through `--collapse` and `--refine`.

The live interactive brush uses the same Inflate law and optional
curvature, stretch-driven refinement, local flipping and directional
collapse mechanisms as the scripted demo. These topology operations
remain **opt-in**; try
`./build/Mesher --refine --collapse --curvature-correction`.

The previous 105-stamp reproducible animation remains available under
`--demo`, including Space to pause, Right to single-step, and R to replay.
CSV experiments remain independent of the viewport. `--no-inflate`
opens a non-sculpting display.

The HUD shows live FPS and actual stamp throughput; for mouse mode its
denominator is the **stationary** expected rate, not a hard ceiling, because
spatial movement legitimately increases the number of stamps. Mouse events
are sampled and applied in the normal render loop; there is no independent
timer thread or GPU compute-based sculpting. For high-density models, CPU
raycasting and whole-mesh updates will require acceleration and dirty-region
updates. A stroke is not yet an undoable transaction; tests should be done
on generated meshes.


## Responsive four-dimensional mouse sculpting

The timed `BrushStroke` follows an **exact accumulated polyline in 4D**
(`x,y,z,timeSpeed * t`). `startTimed` produces the initial stamp.
`updateTimedRealtime` records each new input segment, including its
timestamp, without altering the mesh or discarding any sample.
`finishTimedRealtime` records the final endpoint and stops accepting input,
but does **not** discard any unprocessed part of the stroke.

The viewport calls `pumpTimedRealtime()` at most once between rendered
frames. A pump walks as many sub-Step segments as necessary, then applies
**at most one exact Step stamp**, preserving the unused distance across
segment boundaries and corners. If the mouse is moving faster than the
remesher can process, the backlog stays in the ordered segment queue;
the cursor can keep moving and the older samples continue rendering after
mouse-up. Only after the queue is exhausted does the brush emit a terminal
stamp (when not already at the exact endpoint) and call `Brush::end`.
No samples are skipped; processing may lag behind live input on expensive
meshes. A new stroke can start when the previous one has finished draining.
Esc closes the viewport and cancels any remaining queued input.

The on-screen FPS and STAMPS rates measure actual completed operations.
Interactive console output includes `pending_steps` (estimated remaining
grid stamps, plus a possible terminal sample) and `pending_segments`
(input control segments still to be processed). These help distinguish
CPU performance from queueing delays and show explicitly when a stroke
continues after mouse release. Raw GLFW cursor events are currently picked
once per viewport iteration, not each operating-system event; connecting
successive sampled 3D positions is exact, with no shortcutting or
sample dropping *between those samples*. Faster ray picking would allow
higher-frequency path capture.

The untimed scripted demonstrations and `updateTimed` / `finishTimed`
continue to process all samples synchronously. CPU tests compare that
reference implementation against the one-stamp-per-frame queued sampler
for long movements, zigzag/corner paths, stationary time, mirrored points,
and terminal release.

## Spatial acceleration and 100,000-vertex performance

Sphere relaxation already used a uniform spatial hash and a **2.2-edge-length**
neighbour cutoff; it did not check every pair of points. The main quadratic
initialization bottleneck was the subsequent incremental convex hull, which
scanned all existing faces whenever it inserted a point. Mesher now prefers
the reentrant **Qhull** Quickhull implementation, preserving the sphere's
original vertex positions and outward triangle winding. The historical hull
remains a fallback if Qhull is unavailable, but may be extremely slow on
100,000+ vertices. On Debian/Ubuntu install `libqhull-dev` before building;
CMake reports whether it found Qhull.

A reusable 3D spatial hash partitions vertex coordinates into cells of
0.15 model units. The index supports exact spherical neighbourhood queries,
rejecting candidates by **squared distance** first, as well as incremental
updates when vertices move, and full rebuilds when topology changes.
Its cells accommodate points moving beyond the original sphere bounds.
Inflate now evaluates normal and cotangent curvature terms **only for
vertices inside the union of its influence spheres**, using each candidate's
incident faces. All displacement vectors are still computed before any
vertices move, so brush stamp simultaneity is preserved.

`mesher_spatial_tests` compares queries against a brute-force ground truth,
including vertex movement across cell boundaries. The `mesher_benchmark`
CPU executable reports separate wall times for sphere generation, two
Inflate stamps, and two Inflate + remeshing stamps:

```sh
./build/mesher_benchmark 100000 65
```

The CI workflow runs that case and the existing mesh-generation and sculpt
regressions. Timings vary by host hardware; the benchmark is diagnostic,
not an absolute performance gate.

**Known remaining bottlenecks:** the topology editor still searches the
entire mesh for candidates and validates/copies large trial meshes during
edge collapses and flips; picking still ray-tests every triangle; and the
Vulkan renderer rebuilds full vertex buffers after each stamp. These paths
are not made local simply by accelerating brush vertex queries. A proper
local adjacency/edge index and triangle BVH, plus GPU dirty-region updates,
remain the next optimization phase.


### Large-mesh topology validation batching

For meshes above 10,000 vertices, the experimental remesher now batches
candidate edge collapses and local flips and performs one global closed-manifold
check after the batch rather than after *every* accepted candidate.
Individual edge-link and face-orientation checks remain in place. If the final
batch validation fails, the mesh is restored to its pre-batch state and the
operation is retried using the original conservative per-operation validation.
The small-mesh path is unchanged, which keeps the existing demonstration
regressions directly comparable. This is an intermediate optimization:
collapser link checks and trial mesh copies still have whole-mesh costs, and
eventual production topology editing needs persistent local adjacency.


### Optional parallel relaxation

The existing 2.2-spacing-radius relaxation remains a local-neighbour
algorithm. With OpenMP available, Mesher also distributes sufficiently
large relaxation iterations across CPU cores. Each worker reads the same
immutable per-iteration spatial hash and writes a distinct next-position
slot, preserving the iteration's Jacobi semantics. The serial path remains
available when OpenMP is absent. CMake detects OpenMP automatically.


### Exact stamp-local refinement candidate search

An edge's length cannot change when both endpoints stay still. Inflate now
passes the face IDs incident to its moved vertices into the refiner, which
inspects only edges belonging to those faces when identifying stretched-edge
candidates. The geometry and reflected-topology acceptance rules are
otherwise unchanged. Scripted runs and standalone refiners retain their
original full-mesh scan path. A regression compares local versus global
candidate searches on the same deformation and requires identical vertices,
triangle connectivity and operation counts.


## Verified large-sphere initialization and build diagnostics (2026-10-15)

**Bug fix:** `src/hull_qhull.cpp` had been checked in but had not actually
been compiled or linked by CMake. Therefore previous CI measurements used the
quadratic legacy hull, **not** Quickhull. The build now discovers and links
`Qhull::qhull_r` when available, compiles that source, and selects Quickhull
at runtime. Unset build types on single-configuration generators now default
to Release (explicit Debug or other configurations remain respected).

On Ubuntu/Debian:

```sh
sudo apt install libqhull-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/Mesher --vertices 100000 --refine --collapse --curvature-correction
```

Configure output reports the selected build type and detected hull backend.
Program startup reports the build configuration, actual hull backend,
maximum available OpenMP threads, and times for relaxation, hull
triangulation, manifold validation, and total generation. The standalone
benchmark also emits these stage measurements on stderr. The old
quadratic fallback is retained only for meshes of **10,000 or fewer
vertices**; larger requests without Qhull fail before relaxation with
install instructions, rather than silently taking many minutes.

CI asserts that the 100k benchmark uses Qhull, and separately builds with
Qhull disabled to verify the large-mesh fail-fast behavior. The earlier
Release-build CI measurements of 11–20 seconds used the old quadratic
backend and should not be represented as Quickhull timings.


## Persistent local refinement connectivity

The stretch refiner now maintains an edge-to-incident-face map and a
vertex-to-incident-face map **across brush stamps**. Splitting an edge updates
only its two incident faces and their new triangles. Flipping a diagonal
changes only its four local face/vertex relationships (including its mirrored
counterpart); the expensive full-mesh trial copy per candidate is gone.
One-ring flip candidate discovery also starts at the touched vertices
instead of rescanning all triangles, and vertex valence is obtained from
the adjacency map. Triangle quality, curvature/normal gates, mirrored
pairing, deterministic ordering, and topology validation remain unchanged.

The expensive initial index build happens at first use, and the cache stays
valid through subsequent refinement stamps. After a directional collapse
compacts/renumbers the vertices and triangles, `setMirrors()` invalidates
the index; it is lazily rebuilt on the next refinement. The small-mesh path
retains strict per-flip manifold checks. Dense meshes validate the entire
flip batch, with one pre-batch backup and a conservative per-flip retry on
failure. This preserves safety without copying the complete 100k-vertex
mesh for every candidate.

The `local_refinement` suite includes repeated-stamp equivalence checks
against an intentionally rebuilt index, comparing the exact vertex
positions, triangle order/orientation, topology operation counts and
symmetric mirror mapping. Note that the collapse component itself still
uses global face scans and compaction, and CPU picking/render uploads
remain whole-mesh operations; this patch specifically makes splitting
and flipping local.


## Interactive brush radius and per-stamp console diagnostics

Interactive mouse sculpting provides a vertical brush-radius slider in the
**lower-left** of the Vulkan viewport. Its track is **one third of the
framebuffer height**. The top corresponds to radius `0.5`; the bottom
corresponds to nominal initial edge length
`sqrt(8*pi/(n*sqrt(3)))`, clamped to `0.5` for tiny meshes. Here `n`
is the **vertex count supplied on startup**, not the changing vertex count
after remeshing. The initial slider value is the existing
`--brush-radius` parameter (default `0.4`), clamped into that range.
Dragging the slider cannot start a sculpt stroke; if adjusted mid-stroke,
its radius applies to subsequent stamps without resetting 4D Step phase.
The radius readout uses the same lightweight Vulkan HUD path as FPS.

To help diagnose 100k-vertex interactive performance, **after each
completed mouse stamp**, if at least **250 milliseconds** have passed since
the previous status line, Mesher writes a single
`[sculpt] ...` line to stdout, flushed immediately. These lines form
a copyable timeline. The console counters have their own actual elapsed
interval, independent of the existing 750ms on-screen HUD averaging.
Each line contains: elapsed startup time, interval length, actual
presented FPS, completed stamps per second, total and skipped stamp
counts; last mouse raycast time, brush callback time, full mesh buffer
rebuild/upload time and most recent frame submission/presentation time;
the **actual interpolated brush center(s)** used for the last stamp,
the **actual radius**, counts of unique vertices geometrically inside
the union of brush spheres and of vertices that received a displacement,
counts of split/flip/collapse operations, post-stamp vertex and face
counts, and Inflate/refine/collapse/adjacency-rebuild CPU times.

`influenced_vertices` is the precise de-duplicated spatial-query candidate
count before deformation; it is **not** the sum of counts in overlapping
mirrored spheres. `moved_vertices` excludes zero-influence or otherwise
unmovable vertices. Brush timing is per stamp; FPS and stamps/second use
the interval since the previous console line. Mouse picking and Vulkan
render-buffer updates are measured separately because they are not
included in the headless `mesher_benchmark` CPU timings. No console
status lines are generated during idle frames.


## Verbose mouse input trace

For detailed investigation of stroke backlog, add `--verbose-input` to
**interactive** Mesher sessions. This is intentionally opt-in because printing
every input event can itself perturb performance. It supplements (and does
not replace) the 250ms `[sculpt]` throughput lines:

```sh
build/Mesher --vertices 5000 --brush-radius 0.1 --refine --collapse \
  --curvature-correction --curvature-strength 0.25 --verbose-input \
  2>&1 | tee sculpt.log
```

Each `[input]` event includes a **monotonically increasing viewport-relative
`t`** in seconds, a stroke identifier and event sequence, time since
mouse-down (`stroke_t`), framebuffer cursor coordinates, whether the ray
hit the mesh, the resulting 3D stroke center, and radius. The following
events are logged:

- `event=down` for every detected left mouse-down, including missed
  geometry, slider interaction, or a stroke blocked while old work drains
- `event=move` for any input update whose accumulated 4D arc-length
  crosses one or more new **Step** boundaries, even for a stationary mouse
  where the time coordinate alone progresses
- `event=up` for every detected left mouse-up, reporting the final path
  point and any newly queued steps
- `event=drained` after the released stroke has been fully processed,
  including `drain_delay_s` since release

The log distinguishes `new_steps` produced by this input event and
`input_steps_total` since stroke start from `pending_before` /
`pending_after`, `segments_before` / `segments_after`, and
`rendered_stamps`. `input_steps_total` counts accepted Step boundaries
**without regard to whether the render thread has already caught up**;
the queue may decrease during frame pumping but the counter does not.
All mouse movement between sampled points is preserved by the existing
exact 4D segment queue, with no missing intermediate stamps.

The ordinary `[sculpt]` timestamp now uses a **single, non-resetting
viewport start time**, not the HUD's periodically reset FPS clock. The
250ms FPS / throughput window starts when a stroke starts, rather than
including the seconds spent idle before the first mouse-down. Periodic
lines also contain `input_steps_total`. In this design, a fast gesture
can legitimately queue many Steps and the queue renders **at most one
stamp per frame**; high rendering FPS and a large backlog are therefore
compatible. The verbose trace shows whether the backlog was produced
during input or is already being paid down after release.

GLFW mouse state and position are currently **polled once per render
loop**, not captured for each OS-delivered mouse motion event. These
logs show precisely what input the stroke engine received at that
polling frequency, not a higher-frequency hardware mouse trace.


## Dedicated asynchronous sculpt worker

Interactive Inflate now processes its queued 4D Step samples on a **dedicated
`std::jthread`**, independent of GLFW input polling and Vulkan presentation.
The viewport enqueues `begin`, `update`, `end`, and brush-radius changes
as ordered commands. The worker exclusively owns its mutable `Mesh`,
`AdaptiveInflateBrush` and `BrushStroke`. It consumes samples sequentially
**as quickly as sculpting permits**, without the previous artificial
one-stamp-per-presented-frame limit. The existing 4D arc-length interpolation,
symmetry, skip-free spacing and terminal stamp behavior are unchanged.

The renderer/picker never access the live worker mesh. After completing
stamps, the worker publishes a **copy of a completed mesh state**, coalesced
to roughly one update per 8 milliseconds. The UI takes the newest published
snapshot (intermediate display states may be discarded, but **no actual
brush stamp is dropped**), builds new Vulkan buffers and renders once per
frame. No GLFW or Vulkan calls run on the worker. GPU buffer synchronization
continues to occur on the rendering thread. Mouse picking uses the latest
displayed mesh, so the brush remains responsive while expensive processing
is underway; if processing truly cannot keep up, ordered queued input
accumulates and drains after mouse release. On Escape/shutdown, the worker
is stopped and joined before the brush/mesh owners are destroyed.

The worker publishes thread-safe statistics including completed stamps,
pending 4D Steps, pending input segments and queued input commands. The
`[sculpt]` lines now include `input_commands` and `worker_stamp_ms`;
`enqueue_or_stamp_ms` is the UI callback time (usually just enqueueing).
Counts reflect worker-processed commands and can briefly lag raw
`[input]` events when the background worker is busy. FPS and actual
completed-stamp rate are still calculated from the viewport's presentation
and snapshot consumption intervals, independently of CPU worker progress.

The CPU-only `asynchronous_sculpting` test drives the same path through
both the worker and the synchronous exact sampler, ensures identical
geometry/topology and stamp totals, and verifies that all work completes
**even if no rendering thread consumes snapshots**. It also covers
back-to-back strokes and orderly worker cancellation. The scripted demo,
comparison benchmarks and synchronous `BrushStroke` interface are unchanged.

## Versioned partition storage (V1–V2)

The interactive Vulkan/Inflate code still works with the existing flat
`Mesh{vertices,triangles}`. The following versioned storage is built,
tested and independently usable but **not yet wired to live sculpting**.

- Published `MeshRevision` partition trees are immutable. A mutable view
  copies each edited partition only once, then commit path-copies the
  persistent radix directory. For one modified partition a commit copies
  nine directory nodes regardless of total model size. Atomic publication
  replaces only the revision root; old reader revisions remain valid.
- Stable `VertexId` and `FaceId` values are nonzero 64-bit counters, not
  flat indices or partition coordinates. Revisions derived from the same
  root share an atomic identity domain; discarded IDs are never reused,
  including after abandoned transactions, deletions or branching. IDs
  do not change if a future operation moves an element between partitions.
- `partitionFlatMesh` performs an **initial** Morton spatial sort of
  vertices and face centroids, independently grouping at most 256 of each
  per partition by default. Faces store global stable vertex IDs, so one
  vertex shared across a seam is still a *single* element even when faces
  belong to different partitions. A vertex crossing a spatial cell later
  does not automatically change its partition owner.
- `FlatMeshIndex` stores the old original ordering only to allow
  `reconstructFlatMesh` to reproduce every coordinate bit, face index,
  face ordering and winding exactly. This export deliberately rejects
  missing/extra elements rather than silently dropping changes from a
  newer topology; dynamic topology export is a later milestone.
- `validatePartitionedMesh` checks duplicate/zero IDs and broken
  cross-partition references; it does not establish manifoldness or
  collision freedom. Full partition traversal and import/export are
  intentionally O(N); none is invoked during commit or publish.
- The low-level mutable partition API is not yet protected by validated
  topology transactions. Cross-partition read/write sets, localized
  connectivity/spatial lookup, render updates and retirement policy
  are separate milestones listed in `todo.md`.

## Local partition transactions and boundary integrity (V3)

`RegionTopologyIndex::build(revision)` constructs an immutable global
vertex/face/edge lookup for one published revision. Building it currently
takes O(N) memory and CPU. It can be shared across transactions against that
same revision, and importantly **commit does not rebuild or scan it**.
V4 replaces it with incrementally maintained revision-scoped indices.

A `RegionEdit(index,seeds,mirrors,maximumElementsPerPartition)` discovers
its selected seed vertices (including explicit reflected partners), every
incident face, and the immutable read halo of adjacent vertex/face owners.
Owners of selected vertices and incident faces form the **writable halo**.
Every writable partition is cloned once before any mutation; other partitions
retain identical objects. Raw mutable `MeshPartition` references are never
exported from RegionEdit. Unknown vertices, faces, and partition IDs cannot
be mutated through this API. A default capacity of 256 vertices **and**
256 faces per partition bounds per-partition cloning and growth; callers who
import larger V2 partitions can explicitly opt for a larger capacity.

The transaction provides coordinate changes, adding/removing vertices and
faces, rewriting existing oriented triangles, and a dedicated
`replaceMirroredFaces` operation. Supplying a mirror involution causes
`moveVertex` to move both sides atomically and ensures self-mirrored seam
vertices remain on X=0. A mirrored face-pair replacement verifies the old
faces really are reflected partners and writes the reversed orientation of
the new face to its counterpart. Partial symmetric topology insertions and
deletions are intentionally rejected until V6/V7's paired remesh tools.

Prior to committing the private, already-cloned partitions, validation
reconstructs only the **changed** geometry and edge/vertex neighborhoods
from index entries plus local deltas. New and surviving edges must have
exactly two oppositely oriented incident faces; edges removed entirely
by a legal flip are allowed. Affected vertex links must each form one
connected cycle. Dangling IDs, local degeneracy, broken seam restrictions,
invalid edge incidences and disconnected vertex fans reject the transaction.
No unvalidated intermediate mesh can reach the published revision root.
This is a closed-manifold local editing policy for the current prototype,
not comprehensive intersection detection, a guarantee of global manifoldness,
or the eventual arbitrary-open-scan editing policy.

After successful validation, `RegionEdit::commit` uses V1's O(D*H)
directory path copying and returns a new immutable revision without
publication. `MeshRepository::publishIfCurrent(old,new)` is an atomic
compare-and-swap that refuses to overwrite a newer revision when a worker
prepared an edit from an obsolete base. Readers retain older roots safely.
`MeshRepository::publish` continues to serve the existing single-writer
path. This V3 engine is independent of the current interactive sculpt worker
and Vulkan viewport; the worker switch-over and dirty GPU updates are V8/V9.

## Incremental, revision-scoped spatial and connectivity indices (V4)

`RegionTopologyIndex` is now a persistent index of stable vertex and face
IDs, oriented edge-to-face incidence, each vertex's incident face IDs, and a
3D spatial grid. Every mapping is represented by an immutable, structurally
shared AVL tree (`PersistentIndexMap`). Replacing or removing one entry
allocates O(log N) tree nodes and leaves the old root and unchanged record
pointers intact; it does not copy a global hash map or accumulate an
ever-growing ancestry/overlay chain. One-time `build()` traverses an imported
revision and constructs these indices, O(N log N) including tree insertion.

Each validated `RegionEdit::commit()` now derives a corresponding index
from its local vertex/face delta. It subtracts incidence for old versions of
changed faces, adds new edge/face/vertex incidences, and moves affected vertex
IDs between old/new spatial buckets. Unmodified records retain pointer
identity. `updatedIndex()` is available after commit, and always references
**exactly** the committed immutable `MeshRevision`. The
`IndexUpdateStatistics` information is exposed as
`RegionTopologyIndex::UpdateStatistics`: the number of changed vertex,
face, edge and cell records, copied tree nodes, and whether a full mesh
traversal was needed. An incremental update reports zero full traversals.

`IndexedMeshRepository` offers one atomic compare-and-swap of the index
root, which owns its matching immutable mesh revision. This prevents a
reader from pairing an old geometry root with a newer index (or vice versa),
and prevents a stale writer from replacing a newer published state.

`querySphere` enumerates spatial buckets and performs exact distance
filtering against the chosen revision. Empty-space iteration is bounded:
for very large query volumes it scans the *occupied cells* rather than
millions of empty coordinates. `firstRayHit` follows the existing CPU
ray/triangle intersection semantics and takes all geometry from the chosen
revision. **Ray picking is correct but remains linear in total faces**:
the selectively updated triangle acceleration hierarchy is V10.
The current interactive worker and Vulkan renderer are still on the
original flat Mesh path; switching them over is deferred to V5-V10.

V4 tests verify repeated COW AVL rotations/deletions against std::map,
180 consecutive localized revisions with bounded record/path updates,
stable unchanged pointers, exact spatial queries and original CPU picking,
incremental face flips/subdivision matching independently rebuilt
connectivity, and concurrent publication/readers. V4 does not add
self-intersection detection or change geometric remeshing policies.

## Local Inflate deformation and pre-stamp baseline (V5)

`VersionedInflateBrush` performs an Inflate stamp directly on immutable
`RegionTopologyIndex` geometry. Its candidate selection uses the same
maximum-of-influences falloff as `InflateBrush`; the per-vertex unit-face-
normal sums, optional cotangent mean curvature and final displacements
follow the original numerical formulas. Critically, **every influence,
face normal, and curvature measurement uses the same pre-stamp revision**.
All displacements are gathered before any copy-on-write modifications.
`RegionEdit` validates the affected triangles and applies only local
vertex updates, advancing the persistent connectivity/spatial index with
the changed vertex IDs. A stamp with no affected vertices returns exactly
the original index pointer and performs no publication or directory copies.

The returned `LocalStampBaseline` contains a shared ownership reference
to the old immutable topology index plus sorted moved `VertexId` and
incident `FaceId` lists. `positionBefore`, `edgeLengthBefore`,
`edgeWasGrowing` and `edgeWasShrinking` expose the original frame for
directional splitting/collapsing: they compare the prior and new revisions
without allocating a full previous-positions vector. Remeshing against
this baseline will be implemented in V6 and V7.

When an involutive mirror map is supplied, each influenced vertex is
paired with its X-reflected counterpart. Its movement and reflection
commit as a single transaction; a shared X=0 seam remains on the plane.
For mirrored pairs the lower stable-ID side determines the displacement;
the other is reflected exactly. The original flat Inflate calculates
both sides independently, so tiny differences due to floating point
roundoff are possible; face incident order is also different after
Morton partition import. The V5 tests compare against the original
4D-sampled synchronous Inflate geometry with tight numeric tolerances,
rather than claiming bitwise equality across differing summation orders.

The existing `AsyncSculptWorker` and viewport still operate on a mutable
flat `Mesh` (migration is V8/V9). However, the **default** flat
`AdaptiveInflateBrush` immediately benefits from a separate safe
optimization: when both refinement and collapse are disabled, it no
longer makes the previously unconditional whole-array pre-stamp copy.
`StampDetails::preStampCopiedVertices` records how many positions are
actually copied. When either legacy remeshing pass is enabled the full
baseline remains in place, to preserve its exact directional topology
decisions until V6/V7 port those algorithms. No 4D Step sampling,
worker scheduling, or existing geometry algorithms are changed here.

## Transactional local refinement using stable IDs (V6)

`VersionedLocalRefiner` consumes the committed post-Inflate
`RegionTopologyIndex` and the preceding V5 `LocalStampBaseline`. It
uses only the baseline's `affectedFaces` to enumerate unique candidate
edges. An edge splits only if its current length exceeds the nominal edge
target computed from the *original* vertex count and if the old immutable
baseline proves that the same edge grew by more than the original epsilon.
No full-mesh vector, vertex reindexing, or adjacency rebuild is involved.

A split finds the two oriented incident faces via V4 indexed edge-side
references. One private `RegionEdit` replaces both old triangles, appends
two new triangles and one midpoint. Mirrored counterparts are staged in
the same transaction, including two fresh midpoint IDs when needed; a
self-reflecting seam midpoint is shared and constrained to X=0. Everything
is locally validated (including cross-boundary orientation and closed
vertex fans) **before** the next immutable root/index pair exists. A
rejected candidate leaves the prior revision intact. Original IDs are
never renumbered, and the refiner's involutive mirror correspondence
changes only after a successful mirrored commit.

To avoid breaking the fixed partition-size bound after imports in which
every original partition is full, V6 reserves the low 2^31 partition ID
values for V2 imports and allocates overflow partition IDs from the upper
half *downward*. The allocator is shared by all branches of a mesh's
identity domain and never recycles IDs, including abandoned operations.
`RegionEdit::appendLocalVertex/appendLocalFace` first prefer a writable
partition with spare capacity, otherwise allocate a fresh empty overflow
partition without scanning all partitions or enlarging any. New overflow
partitions are committed atomically with the same local topology change.

After accepted splits, flip candidates are gathered from the affected
one-ring using stable IDs and indexed incidence. The existing flat
refiner's acceptance conditions were ported: outward radial winding,
coherent unit face normals, triangle quality, local valence cost, and
absence of an already-existing new diagonal. Only valid candidates are
applied; both halves of a mirrored flip must meet the same conditions
and commit together. The returned `VersionedRefineResult` includes the
updated revision-scoped persistent index, edit statistics, new vertex
IDs, changed face IDs and newly inserted mirror pairs. Feeding those
pairs to `VersionedInflateBrush::addMirrorPair` supports subsequent
symmetric deformation without reconstructing the entire mirror map.

The existing `LocalRefiner` and `AsyncSculptWorker` still use their
original flat-indexed geometry path; V7 will port local collapsing and
V8 will cut over the asynchronous worker. The current V2 flat export
mapping describes *only* its initial topology and deliberately refuses
new elements. V6 tests reconstruct the current version dynamically for
independent manifold checks; production general export and accelerated
picking remain later integration tasks. No self-intersection guarantee
is implied by local manifold validation.

## Stable-ID transactional directional collapse (V7)

`VersionedDirectionalCollapser` replaces the flat `DirectionalCollapser`'s
full-mesh trial copies with one private `RegionEdit` for each eligible
localized candidate. It uses V5's immutable pre-stamp baseline for the
same *short AND shrinking* condition as the established algorithm,
including the original nominal length threshold and epsilon. Candidates
are drawn from the stamp's affected face IDs, and neighbor/link conditions
are looked up through V4's revision-specific vertex and edge index rather
than scanning every triangle. The indexed link condition requires two
incident edge faces with exactly their two opposite vertices in common.
The existing midpoint preference, radial orientation and minimum triangle
area rejection criteria remain in place.

Each accepted trial stages the survivor's new position, all affected face
redirects, exactly the faces that become degenerate, and removal of the
discarded vertex IDs. A mirrored pair of operations is staged together,
including the special case of preserving an existing seam vertex's position
instead of moving it off the X=0 plane. V3 verifies the complete changed
edge-and-one-ring topology before it can commit. Rejected transactions
never alter the current immutable root/index, and a successful commit
modifies only bounded writable partitions. There is no temporary full
`Mesh` copy, no `compact()`, no whole-mesh connectivity rebuild and no
array-index rewriting for survivors. Each new persistent index records
retired vertex/face IDs as structurally shared tombstones; previous
revisions still retain the live objects and identity allocation never
reuses retired or abandoned IDs. Tombstone retirement/reclamation is
covered by V11, not an unbounded retention promise.

`VersionedCollapseResult` returns the current immutable index, aggregate
operation statistics, retired vertex IDs, retired and rewritten face IDs,
changed directory/partition counts and retired mirror pairs. The
`removeMirrorPair` methods in the V5 Inflate and V6 Refiner state objects
accept those pairs incrementally so subsequent symmetric stamps cannot
accidentally reference a retired vertex. Collapses performed within a
stamp see the previous successful operation's updated index; candidates
whose old endpoints have been removed are skipped.

The live asynchronous sculpt worker continues to use the old flat
`DirectionalCollapser` through V8, preserving currently shipped brush
semantics until the complete V5-V7 pipeline is integrated. The new
fourteenth CTest suite exercises accepted local and mirrored collapses,
old-revision stability, persistent ID tombstones, local partition sharing,
manifold checks, independent index rebuild equivalence, repeated
candidate trials, and post-collapse symmetric Inflate integration.

V7 does **not** establish self-intersection freedom, general support for
nonmanifold or open scans, global topology validation during each stamp,
or deterministic equality of every legacy sequence despite differences
in stable-ID ordering and local candidate restriction. Those are distinct
contracts and must not be inferred from closed-manifold local tests.

## V8: Asynchronous publication of immutable indexed mesh revisions

The interactive worker now calls `VersionedSculptBrush`, which performs
V5 Inflate, V6 splitting/flipping and V7 collapse within its dedicated
thread. It reuses the unchanged `BrushStroke` queue and 4D Step sampler:
each mouse-down, pending arc-length sample, stationary time sample and
terminal position is processed once, independent of rendering frequency.
The BrushStroke's `Mesh` parameter is an empty placeholder; no mutable
whole-mesh geometry is passed through that interface.

A complete stamp uses one immutable pre-edit baseline for all stages.
Newly inserted mirror pairs reach the collapser before its pass, and
deleted mirror pairs are removed from Inflate/Refinement afterward.
The worker atomically publishes only the resulting complete mesh/index
pair through `IndexedMeshRepository`. Intermediate operations remain
private. Publication and the coalesced snapshot mailbox exchange
shared immutable root pointers, not copied vertex or triangle arrays.
Unconsumed visual revisions may be coalesced, but sampled stamps are
never skipped and readers can retain previous roots safely.

The Vulkan renderer still consumes flat `Mesh` geometry. V8's explicit
`flattenRevisionForViewport` adapter runs only on the viewport's
snapshot-consumption path, never during worker stamp/publication.
It traverses the chosen immutable revision, retains the original flat
ordering for surviving imported IDs and appends new elements in ascending
stable-ID order, while remapping triangle indices. The adapter measures
`viewport_flatten_ms` and `viewport_flatten_bytes`; worker status
also tracks cumulative edited partitions, directory-node copies and
published revisions. The remaining full-mesh conversion and Vulkan
buffer upload are intentional V9 costs, and UI frames can still stall
on large meshes. V11 will improve old-root reclamation and stress metrics.

Async regression tests exercise exact input-stamp counts, geometry
comparison against the established synchronous sampling path, retained
immutable snapshots, nonblocking coalescing, enabled versioned remeshing,
and compatibility conversion after local topology growth.

## V9: Dirty-partition Vulkan drawing

The interactive viewport no longer requires a flat `Mesh` conversion
when a worker revision arrives. On startup only, it traverses the initial
immutable revision to build `PartitionRenderCache` CPU geometry and paired
Vulkan solid/wire buffers for every face-owning partition. Subsequent
`ViewportMeshUpdate` values contain exactly the revision root and the
union of dirty partition IDs since the previous *consumed* frame; the
worker coalesces these deltas even when it skips intermediate visual
revisions. The first mouse-down stamp is included explicitly because
`BrushStroke::startTimed` executes it synchronously outside the usual
pump method. Every queued 4D Step stamp is still processed by the worker.

`VersionedSculptBrush` gathers moved vertex IDs, split-inserted and
collapse-retired vertex IDs, pre-stamp affected face IDs and all
replaced/inserted/deleted face IDs. `dirtyRenderPartitions` visits the
before/after indexed incident-face and one-ring neighborhood, collecting
face-owner partitions of triangles whose vertex positions, face winding,
normal vectors or wire ownership might have changed. It does not scan
the whole partition directory during stamp processing. The renderer
rebuilds only named dirty partition geometry, reusing exact unchanged
CPU geometry pointers and preserving all unrelated GPU allocations.

Each triangle lives in its stable face-owner partition and carries
the same flat triangle normal as the legacy renderer, even if its
three vertices belong to other physical partitions. An undirected
wire edge is drawn exactly once: its canonical owner is the incident
face having the numerically smallest stable `FaceId`. Consequently
cross-boundary edges are complete and nonduplicated even after flips,
splits and collapses. The floor grid and HUD continue to use their
existing separate buffers and shaders, while the synchronous demo
retains its original flat-mesh renderer.

Before replacing, mapping or destroying any dirty vertex buffer,
the viewport waits on the existing one-in-flight graphics fence,
ensuring that the last submitted draw is no longer accessing the
old GPU memory. Empty/deleted partitions have their handles retired
only after that fence. Swapchain resize still waits for device idle
and then retains the existing partition allocations across recreation;
shutdown waits for device idle before destroying all buffers. The
current one-fence submission model is conservative rather than a
general GPU resource retirement queue (planned V11).

The viewport now uses `RegionTopologyIndex::firstRayHit` against
the *displayed* revision, not a stale old flat mesh, to select stroke
centers consistently with visible geometry. This is exact but still
linear in face count until V10. Display logs report the updated
partition count, rebuilt face count, byte footprint of dirty data
and total update time. No full viewport flatten is performed on
ordinary interactive updates. Per-frame draw submission still walks
the retained GPU partition handle map, with one draw call per solid
and wire partition; large-scale draw-call batching and retirement
optimization are independent future work.

The fifteenth CPU regression suite compares the incremental cache
against independent full rebuilds after local vertex displacement,
cross-partition splitting, directional collapsing and coalesced edits.
It checks no duplicate or missing wire edges, dirty partition bounds,
GPU-payload byte accounting and retained object identity for untouched
partitions. CI compiles the full Vulkan application on GCC, Clang and
MSVC, but does not execute GPU presentation in its headless jobs.
Actual window resizing, rapid input and driver lifetime behavior
must additionally be checked interactively on real hardware.

## V10: Immutable revision-scoped accelerated triangle picking

`RegionTopologyIndex` now embeds a revision-specific
`RevisionPickingIndex`. A one-time import constructs a bounded-size BVH
for the faces of every face-owning partition, with each triangle's
exact current positions and its conservative three-dimensional bounds.
Above the local BVHs sits an eight-level 16-way persistent directory
keyed by the stable 32-bit partition ID. Each directory node stores the
union of its children's AABBs and thus prunes whole sets of face
partitions from ray queries. This directory shares unchanged subtree
pointers across revisions; it does not need an O(total-partitions)
rebuild to accommodate an edit, new overflow partition, or deleted face.

`RegionEdit` already stages exact changes to faces and vertex
positions. On committing a new revision, the picking hierarchy collects
only affected face-owner partition IDs: the old and new owners of
changed face IDs plus owners of incident faces of changed vertices
in both the prior and new connectivity indexes. It reconstructs just
those partitions' bounded local triangle BVHs and copies at most nine
16-way directory nodes per affected owner (root and eight levels).
Empty-face partitions remove their directory leaf; all old reader-held
roots still contain their former face geometry and can answer queries
independently. No full mesh, partition-directory, or overlay-chain
traversal occurs during an indexed region commit.

The ray test preserves the previous double-sided nearest-triangle
Möller–Trumbore predicates, including `1e-13` parallel rejection,
`1e-12` barycentric tolerance and positive hit distance `>1e-8`.
Ray/AABB checks expand bounds slightly to avoid discarding an edge hit
that the final triangle test would accept. The nearest positive hit
still wins even when the camera is inside a mesh. A ray that misses
returns no hit. The public `RegionTopologyIndex::firstRayHit` API is
unchanged for ordinary callers, but accepts optional
`PickStatistics` reporting tested bounds, candidate partitions and
triangles. Its existing invalid-ray validation remains intact.

The Vulkan viewport already retains the index belonging to the
*displayed* immutable revision (V9); V10 simply accelerates that
same reference. Processing further input/stamps on the worker cannot
change the visible mesh's pick geometry mid-frame. The existing 4D
`BrushStroke` sampling, input commands, stamp queue and mouse release
drain are unmodified. Verbose sculpt logs now include
`picking_boxes`, `picking_partitions` and `picking_triangles`,
alongside pick time and pending input counters. Thus backlogged
stamps may delay presentation but cannot force picking to read a
partially committed or future revision.

The sixteenth CPU suite compares BVH hits against independent
exhaustive triangle tests on dense meshes, camera and backface rays,
immutable prior roots, divergent branches and cross-partition
split/collapse edits. It asserts that typical center-screen rays
visit a fraction of all faces, and that local commits rebuild
only a bounded number of partition BVHs with zero full traversals.
The upper tree groups by stable partition ID rather than an
optimal global centroid order. Initial spatial partition sorting
makes ordinary sphere picking effective; extremely distorted or
widely scattered partition IDs may yield poorer pruning without
sacrificing exactness. Accelerated picking does not imply GPU
render/pick synchronization beyond the existing displayed-root
contract, nor collision-free geometry or bounded old-root
reclamation (V11).

## V11: Bounded revision retirement and measured CPU/GPU lifetimes

The V1–V10 immutable mesh architecture intentionally allows readers to keep
any published `RegionTopologyIndex` and its matching `MeshRevision`.
Dropping the last old-root reference may recursively destruct numerous
structurally shared directory, AVL, spatial and BVH nodes. If that happens
in the viewport thread on every consumed revision, it can create
unpredictable frame-duration spikes unrelated to sculpt computation.

`RevisionRetirementQueue` transfers the viewport's replaced display-root
and the `PartitionRenderCache`'s previous revision pointer to a dedicated
`std::jthread`. That thread alone drops those queued references. Queue
capacity is explicit and fixed (64 roots in the application, configurable
for stress tests). Saturation **backpressures** the producer rather than
unboundedly buffering old graphs. Normally enqueueing takes only the
short mutex handoff; when the releaser cannot keep up, a producer can
still wait. This is an unavoidable throughput-vs-memory bound, not a
promise that an arbitrarily slow reclamation thread never affects a frame.
An independent reader may keep a root after it has left the queue, and
the app cannot reclaim that reader's objects early. V12 will define
explicit memory/history budgets and durable checkpoint policy. Normal
root publication remains an atomic pointer update and does not enumerate
the mesh or its directories.

Vulkan retains the existing single-in-flight graphics fence discipline.
When an active solid/wire buffer must grow, disappear, or become empty,
its previous GPU allocation is put into a short per-frame retirement
list. The viewport drains that list only **after** waiting for completion
of the graphics submission that might have referenced the old handle.
Swapchain recreation still waits for device idle; shutdown waits for
device idle, drains pending retired buffers and destroys active buffers
before the device. Unmodified buffers are never reallocated. The list
is scoped to the frame's replaced geometry, not a growing queue of
GPU allocations across frames.

Diagnostics intentionally measure different quantities rather than
claiming one exact global memory total:

- `cloned_partition_bytes`: cumulative actual vertex/face payload
  sizes copied when an existing partition is cloned for COW. It does
  not include allocator padding, persistent AVL node sizes, or retained
  history overhead.
- `modified_partitions` and `copied_directory_nodes`: existing
  cumulative structural-sharing work counts.
- `worker_publication_ms`: elapsed time to publish and coalesce the
  latest immutable worker snapshot.
- `live_revisions` and `peak_live_revisions`: counts of live
  `RegionTopologyIndex` objects, including reader-held old roots.
  The counts are not bytes.
- `retire_queue_roots`, `retire_queue_peak` and
  `retire_backpressure`: bounded application reclaimer queue status.
- `gpu_resident_bytes`, `gpu_peak_bytes`,
  `gpu_pending_retirement_bytes` and `gpu_freed_bytes`: Vulkan
  allocation requirement sizes of currently retained GPU vertex
  buffers, their historical peaks, pending-fence retirement and
  released allocations, respectively.
- `gpu_fence_wait_ms`: time waiting for the prior graphics submission
  before dirty-partition resource updates.

The seventeenth CTest suite drives 160 consecutive local COW edits and
retirements using a capacity-3 queue, validates retained older
topology/picking roots and structural sharing, tests a concurrent
reader/publisher across 60 indexed commits, verifies queue limits and
successful final draining, and cancels a background sculpt worker with
queued input while retaining a previously published reader snapshot.
The earlier 16 suites continue to protect sampled Step input, mirrored
topology, remeshing, dirty rendering and exact BVH picking.

No test or memory counter implies hard bounds on third-party
`shared_ptr` retention, mesh geometry itself, COW copy payloads per
arbitrarily large selected region, or GPU allocation peak during one
replacement. The current conservative fence synchronization and
per-partition draw call volume also still affect huge-model throughput.
Headless CI checks all CPU paths and compiles Vulkan on three toolchains;
on-device rapid input, resizing and shutdown must additionally be
verified with physical GPU drivers.

## V12: Bounded persistent edit history and durable recovery

The active `VersionedSculptBrush` now owns a `RevisionHistory` of complete,
validated immutable `RegionTopologyIndex` roots. Each geometry-changing
4D Step adds one entry with its touched render-partition IDs and the
mirrored stable-ID pairs added or retired by V6/V7. If a midpoint pair
is both inserted and collapsed during the same Step, its deltas cancel,
so history represents the actual transition between two published roots,
not transient topology inside that stamp. Vertex/face counts are stored
with the entry. Reverting a Step swaps the exact old immutable root back
through the single-writer CAS, reverses mirror-pair deltas in Inflate,
refinement and collapse, and sends the existing dirty partition set to
V9's coalescing mailbox. Redo reapplies the same transition. A new stamp
after undo deletes the abandoned redo branch and uses the mesh identity
domain shared with previous branches, ensuring IDs remain monotonic and
never recycle even for abandoned topology.

History retains at most 64 roots and 32 MiB of cumulative **COW copied
partition payload estimates**, whichever limit is met first, always
retaining the current root even when one stamp exceeds the byte budget.
The counters exclude AVL/BVH node overhead, allocator fragmentation,
the base mesh and snapshots held by independent reader threads; thus
the limit is on retained history *deltas*, not a hard process RSS cap.
Evicted roots are passed to V11's bounded retirement thread instead of
being destructed on the viewport thread. `HistoryStatistics` exposes
root count, estimated payload bytes, undo/redo depth and abandoned
entries. The existing `BrushStroke` sampler and mouse input queue
are unchanged; new undo/redo commands are processed by the same
asynchronous worker **between strokes**, never partway through a Step.
Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z) are bound to these commands in the
interactive viewport. This implementation undoes one geometry-changing
Step at a time, not an entire mouse stroke as one unit.

The `DurableCheckpoint` format is an explicitly versioned, checksummed,
little-endian binary serialization of every live partition, vertex
position and stable ID, triangle ID and vertex references, persistent
deleted-ID tombstones, stable-ID allocator watermarks (including
aborted/rolled-back allocations and high-range overflow partition
numbers), the original mesh density used by split/collapse thresholds,
and current mirror-pair correspondences. Loading reconstructs
the original sparse partition directory with the same live IDs, then
builds the versioned adjacency/spatial/picking indices once on the
loaded geometry. It validates counts, uniqueness, finite coordinates,
face references, mirror mappings, checksum and trailing bytes before
returning the recovered index. Fresh allocations after reload start
above the restored high-water marks, not above only the maximum
currently living ID.

`AsyncCheckpointWriter` accepts immutable snapshot handles with a
destination path. Its single background thread serializes the
full graph while local brush edits proceed; new requests coalesce the
single pending request to the latest revision. References to superseded
requests are handed to a separate bounded retirement queue. The writer
flushes to `<destination>.tmp` and atomically renames the completed
checkpoint over the previous one, so a failed partial write never
becomes the advertised checkpoint. POSIX builds fsync the completed
file and containing directory; Windows builds also call
FlushFileBuffers on the completed temporary file before rename.
Directory-entry durability and sudden-power-loss guarantees remain
filesystem/platform dependent. A checkpoint always represents a complete
immutable revision regardless of edits or cursor movement occurring
concurrently during the save. A failed write increments an observable
error counter without terminating or blocking the sculpt worker.

`--checkpoint FILE` chooses where Ctrl+S writes (default
`mesher-checkpoint.mshr` in the working directory).
`--recover FILE` loads that file at startup, bypasses fresh sphere
generation, configures symmetry from the recovered correspondences
and creates a new active versioned worker on the existing stable-ID
geometry. `--recover` is supported in interactive Inflate mode, not
the separate legacy benchmark/demo paths. Recovery intentionally starts
a new bounded in-memory edit history at the checkpoint: the prior
undo/redo stack is **not** part of the on-disk format. That distinction
keeps routine checkpoint size proportional to one current model rather
than all historic versions and makes V11 garbage collection practical.

The eighteenth CPU suite covers bounded history, restoration/branching,
symmetry deltas, corrupt and truncated files, full tombstone/identity
round-trip, exact recovered ray picking, atomic replacement and
background write coalescing. The async tests additionally verify a
genuine worker undo/redo followed by a checkpoint and a resumed stamp.
These tests do not constitute an on-device Vulkan validation of
keyboard shortcuts, crashes during rename or power-loss survival;
those require real OS/GPU exercise and should be explicitly tested.


## Proposed collision-management direction after V12

The new [Sculpt collision and topology repair design](docs/SCULPT_COLLISION_REPAIR.md)
captures the post-V12 discussion of self-intersection detection, positive-
winding occupancy evaluated against **simultaneously deformed proposed
geometry**, approximate density-aware boundary excision/zipper stitching,
and distinct per-tool `Merge geometry` On/Off modes. It explains why
the two intersecting original surfaces in the reference cross-section
continue smoothly through both crossings; the buried red portions are
opposite arcs from their visually corresponding blue extensions.
Neither exact intersection Boolean reconstruction nor full continuous
collision detection is required for the initial interactive approach.
A geometric error budget, repeat-edit drift controls, robust seam
validation, uncertainty reporting and atomic revision publication are
required instead.

The associated [V13-V19 task breakdown](todo.md#self-intersection-aware-sculpting-proposed-v13v19)
is **proposed and unchecked**. V12 remains the newest implemented stage.

## V13: Revision-scoped static triangle collision detector

The first collision-management milestone adds the query-only
`RegionTopologyIndex::triangleIntersections()` interface and
`triangleContact()` narrow phase, with a nineteenth CPU suite. Candidate
triangles are visited through the **existing immutable V10 per-partition
BVHs** of a single complete proposed revision. Both moved and unmoved
surfaces are sampled at their **new** positions; pairs are deduplicated by
stable FaceIds, and normal shared mesh adjacency does not count as
penetration. Coplanar overlaps, contacts and degenerate/ambiguous cases
are separately considered. Index locality and previous root immutability
remain unchanged. There is no live worker wiring, no automatic topology
repair and no proof of perfect exact-predicate behavior. For detailed
limitations and the remaining steps see
[collision-repair design](docs/SCULPT_COLLISION_REPAIR.md) and
[the V14–V19 roadmap](todo.md).

## V14: Post-revision positive winding and read-only surface labels

`RegionTopologyIndex::positiveWindingAt(point)` now calculates the signed
integer winding of the entire immutable revision using the V10/V13 BVH:
deterministic oblique ray crossings contribute +1 for outward departures
and -1 for inward crossings, with at least two independent directions
required to agree. Input exactly on the mesh or grazing triangle edges
is uncertain; an exceptional fallback compares to an independent
O(total faces) long-double solid-angle sum
(`referenceWindingAt()`). In ordinary queries, only BVH candidate faces
are tested and no full graph scan occurs. Winding >0 denotes occupied
solid; both winding 1 and 2 are solid, while 0 and negative winding
are not. The lookup reads ONLY the candidate revision; before-stamp
geometry is irrelevant to occupancy.

`classifyFaceFragments(face,depth)` subdivides a source triangle into
4^depth *sample regions* with original barycentric coordinates and
labels the opposing offset sides as exposed, buried, outside, or
ambiguous. These samples are deliberately not an exact triangle
intersection arrangement. A patch marked buried can still contain a
small visible subregion; no V14 result should itself authorize
deletion. Offsets at two local scales must agree and the winding jump
across an oriented face must be one. Unknown and near-contact results
remain ambiguous. V15 owns conservative actual boundary planning.

`classifyClosedComponent(seed,maxFaces)` traverses local face-edge
adjacency, rejecting open, intersecting or over-budget shells. A
redundant contained positive shell or isolated negative shell can be
reported without a seam; an enclosed cavity *boundary* is reported
as `PotentialCavity`, not silently removed. This distinction alone
cannot prove user intent for arbitrary imported solids.

V14 introduces the twentieth CPU CTest suite. Neither the renderer
nor the sculpt worker calls these APIs, and no mesh topology is
changed. Pathological ambiguity can trigger the O(total faces)
reference; it is NOT a promised hard O(local) query in all situations.
All existing V1–V13 invariants remain unchanged.

## V15: Bounded non-destructive boundary and zipper planning

The new `repair_plan.hpp/.cpp` read-only APIs separate deciding which
faces *might* be removed from how their surviving boundary contours
would be connected. `planApproximateExcision()` seeds bounded local
classification from V13 collision pairs in the complete proposed
revision and follows V14 sampled mostly-buried faces. Explicitly
nominated enclosed redundant components can be removed entirely,
with no seam; potential intentional cavities remain unchanged.
Uncertain winding/contact, topology and work-budget failures cannot
authorize a repair.

`planRemovalContours()` accepts an already proposed sorted set of
stable FaceIds, inspects current oriented edge incidences, traces
closed surviving-face-directed contours through existing VertexIds,
and rejects branching/open/nonmanifold borders. It proposes local
ring pairings by perimeter-resampled positions, cyclic offsets and
either traversal direction, respecting local edge-length-scaled gap
and perimeter bounds. A bounded (at most sixteen contour) global
minimum-cost matching avoids incorrect greedy-first matches and
rejects essentially equivalent ambiguous whole-plan choices.
Complete isolated shells generate no contours and no zipper.
The reference two-crossing surface traces remain smooth through
their intersections; two *local* green seams reconnect the blue
survivors, not a bridge across the red interior.

The output is a **plan only**: no replacement faces, tombstones,
copied partitions, worker transactions or publication. Majority
classifications are merely samples; the computed seam gap and
maximum removed-edge size are not certified surface-error bounds.
V16 must independently validate the actual local cut, replacement
triangles, remaining visible detail, manifoldness and self-intersections
before publishing. The twenty-first CPU CTest covers multi-contour
matching, an intentionally greedy-failing fixture, unpaired/budget
rejections, redundant bubbles, cavity protection and root stability.

## V15.1: Choosing between independent caps and geometric zippering

Two different *valid* topologies can complete the same excision:
independently capping two rings (which may destroy a torus handle),
or joining them with an annular zipper (which may merge distinct walls).
Neither manifoldness nor intersection-freedom alone chooses the
artistically appropriate result. Prefer the repair requiring **less
added surface area**, after strict geometry and topological checks;
do not prefer a topology merely because it preserves old connectivity.

For sufficiently planar compact nearly coaxial rings, use a fast
circular-frustum estimate. Let projected enclosed ring areas be
A1,A2, r_i=sqrt(A_i/pi), area-weighted centroids C_i and separation
d=length(C1-C2). Then estimate
A_caps=A1+A2,
A_zip=pi*(r1+r2)*sqrt(d*d+(r1-r2)^2).
These have identical units (area) and use the same ring dimensions
for both alternatives. For equal circles the inequality simplifies to
d<r for zipper preference; small distant rings naturally select caps.
**No artificial minimum radius is required.** A relative advantage
threshold rejects close decisions. This formula is exact for ideal
parallel coaxial circles and only a preliminary estimate otherwise.
Nonplanar, irregular or tilted/off-axis loops must be rejected or
re-evaluated using more accurate candidate surfaces before committing.

V15.1's closure selector globally compares single-ring cap choices
with paired zipper choices using bounded subset dynamic programming;
it no longer assumes that two or any even number of rings *must* zip.
It retains the existing independent 32-point normalized-arc-length
loop resampling, allowing cyclic offset and reversal. This is used
only for pairing compatibility and the mean/max point-correspondence
diagnostic; vertex list indices must **never** be used to pair original
unevenly tessellated ring vertices. Actual zipper triangles and
caps do not exist until V16.

V16's advancing-front zipper follows physical distance around each
boundary, adaptively splitting exceptional long boundary edges and
updating any retained neighboring triangles sharing those edges in
the SAME RegionEdit transaction. Neither triangle fans of extreme
aspect ratio nor T-junctions are acceptable. The selected closure
is not authoritative until actual cap/zipper triangles have passed
orientation, intersection, error-budget, stable-ID, symmetry and
manifold validation. A repaired torus handle must undergo Euler
characteristic genus 1 -> genus 0; a wide noncolliding tunnel is not
removed just for being narrow. Real brush integration remains V17.

## V16a: Private topology surgery, with full V16 still open

`include/mesher/repair_surgery.hpp` and `src/repair_surgery.cpp`
implement bounded read-only-plan consumption and private geometry
publication preparation. Surgery reconstructs and compares the V15.1
closure selection against the SAME immutable indexed revision, opens
RegionEdit from the surviving/removal face halo, retires selected
faces, divides exceptionally long boundary edges while ALSO
retriangulating their surviving neighbor triangles, adds oriented
ear-clipped cap faces or geometry-driven zipper triangles, retires
orphaned vertices, and commits privately. The ordinary
`IndexedMeshRepository::publishIfCurrent()` is the only path to
publish a successful result, ensuring old render/pick revisions
remain untouched until publication.

The zipper constructs a bounded front grid over the two rings,
choosing quality-bounded triangles by dynamic programming. Its
normalized cumulative arc-length progress is also bounded to prevent
fronts from becoming radically out of phase; this repaired a genuine
crossing-scar counterexample that edge-length criteria alone did not
catch. Only physical edge lengths and geometry enter the score:
unbalanced original vertex counts do not force fan hubs. After
RegionEdit's local two-manifold/one-ring validation, the post-state
V13 BVH is queried for intersections involving new or modified scar
faces. A precise regression fixes V13's false-positive expanded
coplanar clipping near lawful shared vertices.

The 23rd CPU suite covers one cap, two-solid zipper, uneven-density
edge splitting without T-junctions, immutability/retired IDs,
resource-exhaustion rollback and explicit compare-and-swap
publication. The previous torus fitness fixture also executes
actual cap surgery and checks Euler characteristic 0 -> 2. This
is **V16a only**. Full V16 still requires atomic reflected
operations on both sides of X=0, strict residual collision checks
beyond newly inserted surfaces, measured geometric fidelity bounds,
general curved/nonplanar caps and complex repeated repairs.
The current implementation must not be wired to the live brush.

## V16b: Paired topology transactions and local geometric acceptance

RegionEdit now exposes atomic symmetric topology primitives:
`appendMirroredVertices(firstOwner,secondOwner,p)` adds two distinct
off-X=0 vertices at reflected coordinates; `appendSeamVertex(owner,p)`
adds exactly ONE self-reflected stable ID on X=0;
`appendMirroredFaces()`, `removeMirroredFaces()`, and
`removeMirroredVertices()` apply both sides of each paired topology
change. `replaceMirroredFaces()` and moves retain their existing
semantics. Commit checks that each changed face has a reflected
partner with reverse winding, and each updated vertex has reflected
position/existence. A failure mid-paired operation poisons the
unpublished transaction so no half-paired state can commit.

`applyMirroredRepairSurgery()` rechecks V15.1 plan and verifies
the explicitly supplied vertex involution, the reflection of every
removed face, the reflection mapping between contour sets, and
closure-choice invariance. Each distinct mirrored cap pair generates
one representative ear triangulation and its reflected counterpart.
Each pair of independent mirrored zippers uses the ordinary bounded
arc-length front algorithm with automatic reflected triangles.
When two boundary rings directly reflect into each other, they
instead connect through exactly ONE central X=0 seam ring; its
vertices are shared (self-mirrored), with triangles inserted in
properly oriented reflected pairs. There is no index-parity
correspondence and no double seam. Long contour edges are split in
mirrored pairs; both old retained neighboring faces are simultaneously
retriangulated, avoiding hanging nodes and distortion. Unsupported
self-reflected individual rings, edges that cross X=0 during
subdivision, or cases with multiple split edges on one original
triangle are refused, not silently repaired asymmetrically.

For BOTH ordinary and mirrored paths, `validateSurgeryGeometry()`
checks residual contacts involving ALL retained faces incident to
the edit neighborhood, plus all newly introduced faces, against the
complete PRIVATE post-repair indexed revision; no local collision
may survive in that queried neighborhood. It also measures each
new vertex and new face centroid against the nearest original
excised triangle and rejects scar displacement beyond a configurable
multiple of the median local contour edge length. This is a
bounded, one-sided proximity test (not a bidirectional certified
Hausdorff envelope). Validation work is explicitly limited by a
maximum local-face budget. The returned revision has still NOT been
published: the caller must use `publishIfCurrent()` separately.

Sequential independent mirrored cuts, component retirement, and
failure paths retain prior root identity, retire stable IDs without
reuse and preserve manifold orientation. More difficult nonplanar
closures, self-mirrored ring topology and bidirectional preservation
proofs remain necessary for general live integration.

## V16c1: Self-reflected disk caps, multiple boundary splits, reverse detail guard

A symmetric *single* cap ring need not be paired with a second ring.
A valid ring may reflect onto itself. The first supported case has
exactly two original vertices on the plane X=0 and a contiguous
positive-X boundary arc between them. That arc is capped with oriented
ear-clipped triangles, treating the line between the two EXISTING
seam endpoints as an internal edge. Mirroring the triangles with
reversed winding closes the negative-X half across the SAME seam edge,
without doubling vertices or creating coincident sheets. Malformed
rings, more plane crossings and incompatible mirror maps still fail
closed. An octahedral top-hemisphere excision verifies a genus-zero
closed cap, self-mirrored pole retirement, retained old root and both
flat/mildly bent rim geometry.

Original retained faces can have two or three contour edges requiring
subdivision. The old one-split-per-face guard has been replaced by
collecting directed side subdivisions and triangulating the original
face once, with all subedges and their orientations preserved. Both
ordinary and reflected surgery use the same pure triangulator; the
latter reflects the resulting whole face triangulation atomically
into the partner face and uses the same mirrored split vertices.
An internal regression tests two- and three-side triangulation,
surface area conservation, positive orientation and interior edge
incidence, not merely incidental existing fixtures.

Two-sided proximity constraints require an important distinction:
hidden/red faces are INTENTIONALLY erased. A strict removed-to-new
Hausdorff requirement would wrongly forbid the desired topology
change. The default remains *new geometry to removed excision*
sampling. For deletions explicitly intended to preserve exposed
surface detail, `RepairSurgerySettings::requireReverseSurfaceFidelity`
enables old-excised-triangle vertices/centroid → closest updated
local surface distance checks with
`maximumReverseDeviationInEdges`. This is measured against inserted
and retained triangles, with the same bounded local-work budget.
The reverse check is opt-in and rejects entirely discarded shells;
a precise planar cap passes, while a separated-wall zipper subject to
an inappropriately strict preservation demand rejects. Both metrics
are sampled and NOT certified Hausdorff distances.

## V16c2a: Cap feasibility is not zipper-choice fitness

A single surviving removal boundary has no second ring to zipper.
The existing circular-frustum approximation was built for selecting
*between* cap and zipper across TWO loops, and its strict assumptions
must not accidentally become a global ban on legitimate irregular
isolated caps. The V15.1 closure selector now uses distinct
`maximumSingleCapPlanarityInRadii` and
`maximumSingleCapIsoperimetricRatio` bounds ONLY when exactly one
contour exists. These currently default to 0.75 and 3.0.
Multiple loops retain the original tight circular-frustum parameters
and return `AmbiguousClosure` when a distorted loop makes that
approximation unreliable. A single allowed loop is still merely a
CANDIDATE. V16 constructs a real cap, verifies it is a disk through
projection-based ear clipping, and uses full private post-state edge,
vertex-link, orientation, self-collision and deviation checks. In
particular, non-adjacent edges may not intersect in the projected
polygon. Relaxing the heuristic does not relax transaction safety.

For mirrored geometry, the X=0 plane is a *topological* seam: an
existing X=0 vertex must be one self-mirrored stable ID. New symmetric
surgery preflight classifies boundary vertex contacts, straddling
edges, on-plane edges, and near-plane (but non-exact) vertices.
Crossing the symmetry plane without such a vertex is NOT treated as
two separate crossings to be stitched later or snapped onto X=0.
Likewise multi-contact (>2) individual rings and on-plane edges are
currently unsupported and rejected before any repair vertices are
created. This decision is conservative and testable; future explicit
conforming plane splits will require consistent face/edge reflection
and exactly one shared seam ID.

Fixtures: concave prism cap, saddle-warped single isolated opening,
severely warped isolated refusal, warped multi-ring ambiguity,
classified clean seam vs crossing/near/on-plane/multicontact cases.
None invokes the live sculpt worker; the successful revision
remains immutable until an explicit repository compare-and-swap.

## V16c2b1 — Normalize self-reflected crossing edges before contour surgery

A mirror-symmetric triangulated surface can cross the X=0 plane
through an ORIGINAL edge connecting a reflected vertex pair. Both
triangles on that edge can map to themselves under reflection,
including reverse winding. They have NO distinct existing mirrored
face IDs. Treating the edge as two independent boundary edges, or
inserting separate ±0 seam vertices, would break the manifold or
duplicate coincident sheets. A self-reflected face split is a
different operation from the existing paired-face split.

`RegionEdit::splitSelfMirroredFace(face,from,to,midpoint)` requires
that `from` and `to` are exact reflected partners, that the original
face is fixed as an oriented cycle under reflected reverse winding,
and that its third vertex is already self-mirrored on X=0. The
midpoint MUST be a newly allocated single-ID seam vertex at exactly
`(0,from.y,from.z)`; it is not a tolerance-based snap. The
operation replaces the original FaceId with one half triangle,
appends its distinct reverse-reflected second half, and records
both IDs as a mirror pair for RegionEdit's normal commit checks.
Any failure partway through the paired split poisons the transaction.

`normalizeSelfMirroredPlaneEdge(snapshot,first,second,vertexMirrors)`
checks a closed two-sided edge with reflected endpoint mapping and
two self-reflected incident faces, constructs one shared X=0 midpoint,
applies BOTH face splits in a single private RegionEdit, commits only
after local edge incidence and vertex-link validation, and queries
the private post-state index for collisions involving all changed
triangles. It returns a `PlaneCrossingNormalization` with the
updated index/commit, preserved original face IDs, newly inserted
partner face IDs, and exactly one shared seam vertex. A repository
must publish the resulting index separately by optimistic CAS.
An oriented symmetric tetrahedron fixture has two such self-
reflected faces and demonstrates correct four-triangle seam fan,
unchanged Euler characteristic, old-root retention and rollback.

IMPORTANT: This is a standalone prerequisite, not yet wired into
`applyMirroredRepairSurgery`. That operation still refuses crossing
contours until V16c2b2 can transform their removal-face plan onto a
normalized immutable revision, including accounting for newly
created triangles and contour edges. A single local primitive
should never be mistaken for generic arbitrary plane crossing support.

## V16c2b2: Normalize, re-plan, then repair — one atomic publication

A mirrored excision contour may initially cross X=0 through a whole
edge joining an exact mirrored vertex pair, with NO X=0 vertex on
that edge. V16c2b1 can now split such an edge *only when its two
incident triangles are individually self-reflected*. This changes
the retained-face boundary and, critically, can split a DELETED
triangle into two different stable FaceIds. It is incorrect to use
the old removal set or the old contour after such a split.

`repairMirroredContoursAcrossPlane(original,originalPlan,mirrorPairs)`
is the conservative private integration. It checks the source plan
against the exact original immutable index, scans each contour for
crossing edges, and accepts only exact reflected endpoints and
bounded per-contour plane contact counts. The original crossing
edge must be incident to one deleted and one retained face.
Each qualifying edge is normalized into a private revision, with
one self-mirrored seam midpoint. Whenever the preserved half of
a split triangle was marked for removal, the new reflected half is
inserted into the removal set as well. Midpoints extend the mirror
involution and the normalization work counts against the repair
vertex/face budgets.

After ALL crossings are conformed, the operation computes a new
V15.1 plan against the fully normalized index. It checks that all
original ordered ring vertices remain an ordered cyclic subsequence
of their new rings, that the number of rings is unchanged, and
that the cap/zipper choices and pairings agree under the remapping.
An ambiguous or different choice fails closed. The existing V16b
mirrored surgeon then executes the new plan, including normal
manifold, orientation, BVH collision and local drift verification.

Several PRIVATE immutable intermediate roots exist, but none is
published or inserted into user history. The result contains only
the FINAL repair revision as the publish candidate, plus precise
normalization provenance, the new cut plan and aggregate work
statistics. A caller must publish it with
`publishIfCurrent(original,finalIndex)`, never against the last
temporary normalization index. This creates ONE history Step while
preserving all old renderer/picker snapshots.

This deliberately does NOT relax the original fail-closed
`applyMirroredRepairSurgery()` entry point. The new orchestration
supports exactly the self-reflected crossing-edge pattern proven
by V16c2b1, not arbitrary edge crossings whose mirrored partner
is elsewhere in the mesh or loop arrangements with more than two
plane contacts. Subsequent robustness work will consider those
without introducing duplicate plane seams.

## V16c2b3: Which crossings are actually conformable?

There is an algebraic/topological difference between a crossing edge
that is **its own reflection** and an edge whose reflection is
another, topologically distinct edge. For an arbitrary segment
`e=(a,b)` crossing X=0, reflection fixes EVERY point of X=0.
Thus if e crosses the plane at P, its reflected segment crosses
the plane at that exact SAME P. If the reflected edge has different
vertex IDs and is not the same edge, these are two distinct,
nonadjacent edges intersecting at an interior point. The original
mesh is already self-intersecting, independently of how they might
be subdivided. Adding a separate ±0 seam vertex per edge creates
coincident geometry; merging all edge fans at P can pinch a
nonmanifold vertex. Therefore the *normalization* pass should not
guess a weld: it diagnoses the existing intersection and refuses
this case until an actual intersection-excision/retopology strategy
can decide which sheets survive. V16c2b1/b2 correctly handles only
a self-reflected edge orbit whose incident faces can be split
conformingly with exactly one plane vertex.

Likewise an **embedded topological disk** preserved by reflection
has a nonbranching fixed locus, topologically one arc. Its boundary
has exactly two transverse contacts with X=0. Four or more genuine
transverse contacts on ONE invariant ring cannot be made into a
single non-self-intersecting symmetric disk merely by pairing
contact points into chords. Extra contacts may instead be
tangential, which is a separate degeneracy rather than an
additional intersection. A robust preflight now reports transverse
plane-vertex contacts, tangent plane-vertex contacts, strict
crossing edges, wholly on-plane edges, and near-plane numeric
ambiguities, and refuses tangent or over-contact cap candidates
without touching the original immutable mesh.

V16c2b3 regressions mathematically verify distinct mirror-edge
orbits have coincident plane crossing points, demonstrate a
topologically distinct reflected tetrahedral edge pair failing
closed on a real candidate cut, and confirm four crossings on
TWO separate valid rings are not confused with four crossings
on ONE invalid disk. The latter performs the full independent
normalize/re-plan/cap transaction with one final publishable
root and preserves the two-shell Euler characteristic. General
post-excision handling of pre-existing crossing sheets belongs
to separate collision-repair geometry work and is not silently
attempted as seam canonicalization.

## V16d1 — Sampling provenance separately from topological permission

The V14 positive-winding classifier already labels each sampled
barycentric face fragment `Exposed`, `ReversedBoundary`,
`Buried`, `Outside` or `Ambiguous`. A face may be MIXED.
A raw V15.1 `planRemovalContours()` call is a topology proposal,
not evidence that its deletion was justified by collision winding.
To preserve this separation, V16d1 adds a read-only
`inspectRemovalSurfaceProvenance(index,removedFaces,depth,maxFragments)`
returning counts of ORIGINAL-revision sampled fragments by category.
Its bounded sample count and result are observations, not
certified intersection-trimmed polygons or a deletion authorization.

An optional V16d1 `requireExposedFragmentFidelity` policy applies a
reverse-distance guard ONLY to the centroids of the V14 patches
classified Exposed or ReversedBoundary. Original barycentric
coordinates are used to sample the actual original surface, and
distance is measured to the final candidate's retained and newly
inserted local triangles, divided by the local ORIGINAL contour
resolution. Buried/Outside fragments can disappear without an
impossible preservation obligation. Ambiguous patches refuse
the private operation, not silently default to Buried. The
historical `requireReverseSurfaceFidelity` checks every
removed face's vertices and centroid and remains available as
a distinct explicit policy. These two policies are mutually
exclusive, because they express different feature semantics.

Both work budgets are explicit: `maxProvenanceFragments`
bounds original-winding sampling, and `maxReverseDistanceTests`
bounds pairs of reverse samples and local candidate triangles.
Results expose `removalProvenance`, `checkedReverseFragments`,
`reverseDistanceTests` and `maximumReverseDeviationInEdges`.
All validation runs against a PRIVATE committed candidate index;
failure never publishes the candidate. Normal surgery defaults
remain unchanged. Even the selective mode is OPT-IN: neither
whole-face `minimumHiddenFraction` nor a read-only removal plan
is proof that an exposed feature should or should not be erased.

Tests distinguish a cap replacing exposed detail on the outer
cube from a truly buried smaller concentric shell on which both
sides have positive winding. The exposed cap passes an exacting
local reverse bound; complete removal of the buried shell does
not invent an exposure-preservation condition. Conflicting
policy flags and tiny classification/distance budgets reject.
A separate eight-component fixture exercises nearby coplanar
cap replacement against the complete private BVH and confirms
budgeted fail-closed operation with no old-root changes.

This is a sampled, per-patch proximity metric, not a certified
Hausdorff envelope or automatic selection of what to delete.

## V16d2a — An explicit classification-aware caller, not automatic inference

V16d1 exposed a read-only provenance report and an opt-in selective
reverse-fidelity policy. Its low-level `planRemovalContours` caller
may nominate arbitrary faces; it never proves those faces are
buried. Therefore the new `applyClassifiedExcisionRepair` takes
the V13 dirty vertex/face IDs, optional closed component seeds
and explicit `ClassifiedFeaturePolicy` directly. It re-runs
`planApproximateExcision` against the ORIGINAL candidate immutable
index before considering any topology mutation.

The explicit `TopologyOnly` choice preserves the established
V16 default; `PreserveSampledExposedFeatures` adds V16d1's
selective sampled reverse-distance guard. NoChanges, Ambiguous
and BudgetExceeded return the full failed/empty planner result
and no private surgery index. A Ready plan still must pass all
usual private transaction checks, including face/edge links,
orientation, residual BVH intersections and surface displacement.
Neither caller option publishes a worker revision or silently
authorizes deletion based on raw low-level contours.

Even when V14 classifies mixed original patches, V15's
whole-triangle removal is only approximate. A single original
triangle in an overlapping-cubes fixture includes BOTH exposed
and buried subtriangle centroids. No global true/false
feature-preservation flag can accurately express such geometry.
Selected reverse samples preserve *witness points* for exposed
fragments, not a continuous boundary curve or exact Hausdorff
envelope, and no claim of guaranteed preservation follows.

The per-face mixed witness is tested at V14 depth 3 (64 patches).
A separate nested redundant shell test demonstrates an explicit
classified deletion with both caller policy choices, while a
no-change candidate creates no empty revision. Stress holds
six immutable generations alive over five independent face-cap
repairs, checks that stale retired IDs cannot be reused, and
checks retained genus. A near-tangent/coplanar/intersecting
multi-shell matrix exercises bounded classification attempts,
safe failure and unchanged old renderer/picker roots. This
wrapper presently targets NON-SYMMETRIC repair only; V17 must
define the caller's symmetry-aware worker policy separately.

## V16d2b: Cumulative erosion must use an ORIGINAL reference, not a moving one

A succession of individually valid mesh edits can gradually erode
the same feature: bounding scar displacement against the IMMEDIATELY
PREVIOUS revision is insufficient to bound cumulative displacement
against the original surface. Add `sampleSurfaceWitnesses` to capture
seven world-coordinate points per original triangle (three vertices,
three edge midpoints, triangle centroid), and a separate read-only
`auditSurfaceWitnesses` measuring all those ORIGINAL points against
an explicitly nominated set of LIVE final candidate triangles.
The audit returns sample count, unique candidate face count,
distance-evaluation count, maximum, arithmetic-mean and RMS of
closest-point distance, each normalized by an explicit ORIGINAL
local edge length; it uses geometric point-to-triangle distances,
not index-based vertex correspondence. Witnesses can outlive any
number of tombstoned original FaceIds. Audit budgets explicitly
bound both sample acquisition and candidate face comparisons,
and missing/retired faces refuse instead of silently following IDs.

This is an INSTRUMENT and test oracle. It does not implicitly
reject ordinary surgery: a caller must select which original
features should persist, and whether drift on them is permitted.
This cannot simply use all deleted triangles, because buried
material SHOULD disappear. The witness metric is discrete and
one-sided (original samples -> designated current surface). It
does not certify the Hausdorff distance between continuous
surfaces, identify new wrinkles between samples, or replace the
V16 local intersection and topology checks.

The 32-step overlap fixture repeatedly replaces the SAME planar
cube patch with two oriented triangles while holding all 33
immutable revisions. Each step retires two FaceIds, creates two
new face IDs, preserves a closed sphere topology and publishes
with one optimistic CAS. All steps are measured against witness
coordinates captured BEFORE step one. No sampled erosion can
accumulate undetected; an intentionally wrong opposite-face
target yields a large positive distance signal. A deliberately
insufficient distance-work budget and a stale retired-face target
must refuse. A three-body offset overlap matrix exercises V13/V14
collision classification and the explicit V16d2a classified
fidelity policy: ambiguous folded/near-contact candidates may
refuse, but no private operation may mutate its source root.

This does NOT turn the geometry machinery on in the interactive
sculpt worker; that and repeat-contact user-facing policies
remain V17.

## V17e2b2d3b: Explicit worker-serialized single-CAS union publication

The single-writer background `AsyncSculptWorker` now exposes an
**explicit-only** `mergeExactContacts(expected, contacts, limits)` command.
It is NOT connected to an interactive menu, pointer gesture or stamp
collision auto-discovery. The client supplies the full V13 original-contact
graph and the exact immutable root used when that graph was captured.
The command enters the existing FIFO and is refused during a reserved
active stroke. If a queued stamp, undo, redo, or another successful
transaction has replaced that root before execution, a stale expected
pointer refuses the entire Boolean union instead of changing its
meaning to fit newer geometry. A work-budget or validator refusal
likewise returns no renderer snapshot, mirror deltas, undo entry or
partly published mesh.

The worker-owned `VersionedSculptBrush` calls the V17e2b2d3 complete
private preparation. The final original-root Step (not its intermediate
conforming-cut root) is offered once to the same V17c validator that
receives ordinary stamps. On symmetric sessions the original complete
reflected vertex involution is required; missing or inconsistent pairs
cannot turn the operation into an asymmetric weld. Net pair retirements
and additions are validated against ALL THREE existing sculpt mirror
controllers. Refinement and collapse mirror maps stage those changes
under reversible Step journals before one
`IndexedMeshRepository::publishIfCurrent(expected,final)`. A
pre-publication conflict or error rolls back both journals. Following
a successful single-root CAS, Inflate mirror state is synchronized,
the net partition/vertex/face statistics update, and ONE `RevisionHistory`
transition is appended with net pair changes and conservative COW bytes.
The existing single-writer queue prevents other sculpt operations,
undo/redo, or checkpoint requests from observing a partially committed
worker bookkeeping transition. Like the existing stamp path, a
truly exceptional failure AFTER publication is fatal to the worker,
not mislabeled as a recoverable geometry refusal.

The renderer observes ONLY the fully validated terminal index/root
together with the accumulated changed face-owner partition IDs through
its pre-existing coalescing snapshot mailbox. Camera/GL/Vulkan work
remains outside the worker. Existing `undo`/`redo` mirror-delta
replay and `checkpoint` serialization are used without a separate
Boolean-only history or file format. Telemetry has distinct
`completedUnions` and `rejectedUnions` counters, so a failed or
accepted explicit union does not inflate sculpt stamp counts. The
CPU acceptance fixtures queue actual interpenetrating closed
tetrahedra and their X-reflected partner pair, verify one final root,
one undo/redo, mirrored checkpoint restoration, stale-root refusal,
budget refusal followed by a successful command, and unchanged
historical readers. No interactive Merge geometry setting has been
enabled. Repeated sculpt/union acceptance, contact hysteresis,
multi-curve arrangements and symmetry-plane seam junctions remain
open under V17e2b2d4/V17.

## V17e2b2d3: One original-root private Step and net history/checkpoint journals

`prepareExactCutStep` stages the V17e2b2b conforming cut followed by the
V17e2b2d2 EXACT provenance-welded seam join as **two private COW
transactions** and returns ONLY the original `expected` root and the
complete FINAL `final` root. No intermediate cut root, partial deletion
set, or unpublished mirror-map mutation is externally returned. The
terminal immutable revision shares all unchanged partition/picking
directory objects with the historical source. The eventual publisher
may CAS **once** using `IndexedMeshRepository::publishIfCurrent(expected,
final)`; if the published root changed during the private work, the
whole terminal union is stale and rejected. There is no need to copy
the complete mesh into another transaction just to "squash" the two
private operations: original-root pointer identity and one final
published root are the observable Step boundary.

Mirrored Step intent is **explicit**: setting `requireMirrors` means a
missing historical vertex involution is a hard refusal rather than an
implicit conversion into an unrelated nonmirrored union. Geometric
X-symmetry alone is not reliable evidence of the requested topology.

The composer independently repeats the final cut-halo collision query
against the entire terminal mesh and re-evaluates c3's exact-child
cavity-preserving authorization against the SAME cut revision. Every
protected Exposed/ReversedBoundary child must survive with its corner
positions EXACTLY unchanged. Unsupported contacts, budget exhaustion,
invalid journals, and a stale private root return no final commit,
publishable pointer, changed partition, or mirror deltas.

The NET stable-ID journals are computed against the ORIGINAL snapshot,
not by blindly concatenating the two private edit journals. They
contain (1) newly inserted VertexIds and FaceIds still alive in the
final revision; (2) genuinely retired OLD IDs; (3) changed original
FaceIds that remain live; (4) only surviving new mirrored vertex/face
orbits; and (5) paired removals of genuinely retired historical mirror
IDs. Identities allocated and then retired before publication are NOT
exposed as new live IDs or history mirror pairs. Their tombstones and
shared monotone allocator watermarks are nevertheless preserved in the
terminal revision and its durable checkpoint, so they can never be
recycled. Exact partition owners of both cut and joined changes yield
one sorted dirty-partition set without a full-mesh traversal. A
conservative copied-payload accounting sum reflects the cost of both
private COW transactions; history receives one transition with the
final root, dirty set, and NET mirror deltas.

Acceptance covers real closed-tetrahedron unions (2 connected
components -> 1), reflected unions (4 -> 2), success and stale failure
of the original-root indexed-repository CAS, stable old-root
renderer/picker readers, preserved untouched partition pointers,
ONE undo and ONE redo, mirrored pair undo/redo, durable checkpoint
round-trips including tombstones and mirror pairs, and budget refusal
without leaked intermediate roots. This is a **publication-ready data
contract**, not an automatically activated live sculpt operation.
Actual worker integration, history append ordering under concurrent
publication, X=0 seam contacts, cross-section T-junctions and broader
fidelity/load acceptance remain outside this milestone. The desktop
Merge geometry option is still disabled.

## V17e2b2d2: Provenance-only join of c3 cut-surface boundary edges

The original V16 cap/zipper was correctly rejecting the closed-tetra
Boolean because it cannot construct an edge-quality-bounded *strip*
between the two coincident, independent cut sheets. The true union
requires NO inserted strip: after the complete c2/c1/c3 cut, remove
only the certified Buried/Outside REAL child faces and identify
boundary edge chains on the two remaining original source surfaces.

`joinCertifiedCutSections` recomputes full c3 authorization from the
immutable conforming cut root, then enumerates EVERY directed boundary
edge separating a nominated removed child from a surviving child. Each
boundary edge must belong to ONE certified original V13 contact section.
For EACH original transverse pair, both source faces must trace an
entire continuous chain of such child edges along the common finite
section interval, with matching subdivisions, opposite survivor
winding and exactly identical endpoint COORDINATES. The two sides
retain different stable VertexIds until explicit match. A missing,
repeated, partially traced, differently subdivided or geometrically
displaced edge refuses the entire operation. No unrelated co-located
points or spatial nearest-neighbor search can become a seam weld.

Correspondence classes are resolved to stable canonical permanent
VertexIds. In mirrored operations the user-supplied original involution
and the cut's newly inserted vertex-orbit journal together constrain
the choice of representatives on BOTH halves: reflection must commute
with the weld classes. Any self-reflected X=0 orbit or unmatched paired
stable ID is currently unsupported. Every retained incident face
referencing a redundant vertex is rewritten, and only c3-authorized
removed child faces and now-unused VertexIds are retired, in ONE
unpublished RegionEdit. All original and protected FaceIds otherwise
survive. Retiring a would-be canonical seam vertex still used by newly
rewired opposite-sheet faces is forbidden. RegionEdit validates the
entire rewritten surface's edge/fan topology, and the join then audits
all remaining original changed children and new rewired survivors
against the WHOLE final private revision for residual contacts.
The position of EACH protected Exposed/ReversedBoundary triangle
corner must remain EXACTLY unchanged. No added cap or zipper triangles
are needed, so newly introduced surface-distance error is zero on
those protected triangles.

The initial positive regressions are two genuinely overlapping,
closed tetrahedra (nine certified original contact pairs) whose
component count must change **2 -> 1**, plus two independent,
X-reflected copies that must change **4 -> 2** and preserve the
involution. Refusals cover incomplete original section journals,
budget exhaustion and missing historic mirror orbits. All results
stay private: there is no original-root Step CAS, undo/redo entry,
renderer/picker publication, checkpoint or desktop Merge geometry UI.
The supported correctness claim is restricted to the finite-tolerance
transverse V13 contact domain and exactly matching edge subdivisions.
Multi-curve crossing/T-junction generalization, stricter global
fidelity auditing, combined original-root Step journals and
repeat-sculpt desktop acceptance are still required.

## V17e2b2d1: Strict private V16 handoff and final cut-halo audit

The first implementation step after V17e2b2c3 is intentionally a
**fail-closed private surgery adapter**, not an assumption that older V16
cap/zipper strategies automatically produce a Boolean union of intersecting
shells. `attemptPrivateExactCutSurgery` RECOMPUTES the complete original
V13 contact/child-edge coverage and all real-child positive-winding
occupancy from the same immutable V17e2b2b private cut revision, producing
a fresh c3 Ready list of exactly which stable FaceIds V16 may retire.
The V15 contour-scoring settings supplied at authorization must agree with
the V16 settings under which V16 independently reconstructs and checks
the same plan; callers cannot authorize one topology under loose settings
and apply it under different settings.

For symmetry, an explicit, complete original pre-cut VertexId reflection
involution is REQUIRED. The cut transaction's newly allocated mirrored
VertexIds are appended by stable ID (never by nearest/coincident position),
and every map entry must be reciprocal with exactly reflected coordinates.
The old source mesh, read-only cut mesh, any private surgery mesh, and the
eventual live model remain different immutable revision roots. No
`IndexedMeshRepository::publishIfCurrent`, history write, mirror-map
publication, renderer invalidation, or worker action occurs.

The existing `applyRepairSurgery` / `applyMirroredRepairSurgery` receives
the exact c3-authorized FaceId removal plan. Its own RegionEdit validates
face retirement, manifold fans, closure topology, local residual collisions
and scar fidelity. Upon private success, the adapter performs one MORE
bounded collision sweep over EVERY surviving child of every original
changed source face plus EVERY newly generated V16 face, all against the
complete final private revision. It samples seven positions (corners,
edge midpoints and centroid) from EACH c3-protected Exposed or
ReversedBoundary child and checks reverse distance to the final local
triangles under an explicit tiny original-edge-relative tolerance. The
deliberately Buried/Outside faces are excluded from reverse fidelity:
their intentional removal must not be treated as detail loss.

ANY uncertain contour, invalid original mirror map, budget exhaustion,
unclosed or unsupported V16 topology, residual collision or exposed/cavity
fidelity error returns a refusal with **no surgery root**. The two private
edits therefore cannot leak partially into the published model. Passing
this gate alone is not a full historical Step: final stable-ID mirror
journals, independent seam welding where cap/zipper alone is insufficient,
and original-root one-CAS publication remain separate required work.
The current complete interpenetrating closed-tetrahedron acceptance
fixture exercises the real failure boundary: c3 selects exact buried
child FaceIds, but V16 cannot produce an edge-length/quality-bounded
zipper strip between the independent cut-sheet contours. This is an
EXPECTED and correctly fail-closed refusal, not a Boolean union.
V17e2b2d2 must build an explicit contact-provenance-bound seam join,
matching genuine opposing source-sheet cut edges, rather than relax
V16 triangle-quality criteria or cap the wrong contours.

The general V17e2b2d and desktop Merge geometry milestone remain open.

## V17e2b2c3: Cavity-safe exact-child removal planning (private)

`planExactCutRemoval` is the **read-only** bridge between V17e2b2c1
two-sided positive-winding occupancy, V17e2b2c2 complete original-contact
and real-child-edge coverage, and V15's contour-quality assessment. It
RECOMPUTES both certificates from the SAME immutable private
`ConformingCutCandidate`, verifying every stable child FaceId and exact
original provenance; no external caller can inject precomputed winding
labels or a stale certificate as deletion authority. Finite work ceilings
remain active in BOTH prerequisite passes. Unsupported contact domains,
ray ambiguities and budget exhaustion return an empty refusal, never a
partial authorized deletion set.

The sole currently supported policy is
`PositiveWindingUnionPreserveCavities`. For each actual conforming
child triangle, inside is strictly `w>0` on its oriented minus/plus
sides. An oriented unit winding jump is required. `Exposed` (occupied
minus, void plus) and `ReversedBoundary` (void minus, occupied plus)
are **protected**, preserving genuine cavity interfaces even when the
cavity itself has a negative inner shell orientation. Only `Buried`
(occupied on BOTH sides) or `Outside` (void on BOTH sides) are
nominated. Winding parity and whole-original-face hidden fractions have
NO role. `Outside` includes fully negative-winding imaginary shells;
it does NOT mean all inward-facing cavity triangles are disposable.

An explicitly mirrored cut must have a COMPLETE pair of stable FaceIds
for every changed child and a complete exact off-plane reflected pair
for every inserted VertexId. Paired child triangles must have EXACT
reflected positions and opposite cyclic winding; paired winding classes
must agree, and deletions must form complete mirror orbits. A partial
mirror journal or asymmetric occupancy refuses the entire proposal.
The result reports all protected children, the selected exact child
FaceIds and their reflected pairs only AFTER every classification and
topology test succeeds. It also derives a consistent original/new
VertexId mirror mapping from corresponding child corners, pairs the
directed V15 removal contour loops under reversed reflection, and
checks that cap and zipper choices are orbit-compatible; an unmatched
self-reflected contour still fails closed at this stage.

V15's `planRemovalContours` is used on those EXACT FaceIds to establish
legal retained-face boundary loops and a private cap/zipper closure
candidate. A non-Ready contour plan or unsupported geometry is an atomic
refusal and cannot leak a partial deletion set. The output is NOT a
V16 surgery, has not retired any face, does not apply any mirror journal,
and does not modify a renderer, checkpoint, undo stack, worker, or
published root. Closed two-tetrahedron full-contact regression exercises genuine
protected AND removed exact-child FaceIds (16 selected and 27
protected on the original nonmirrored fixture) and the reflected
fixture exercises complete paired face/contour journals. The default
V15 quick area/planarity prefilter may refuse heavily warped loops;
positive tests explicitly supply finite, less restrictive preliminary
V15 shape/gap bounds without bypassing contour topology checks or
altering the production defaults. Unsupported or asymmetric choices
still refuse before reaching V16.

V16 must still perform whole-candidate private surgery,
close contours, preserve exposed original features, verify all residual
intersections and fidelity, and V17 must stage one final original-root
CAS. No desktop Merge geometry control is enabled.

## V17e2b2c2: Independent finite-tolerance contact-coverage certificate

`certifyConformingCutCoverage` accepts ONLY a complete unpublished
`ConformingCutCandidate` with immutable original and new roots, a full
explicit child-to-original FaceId journal and the exact V13 original
collision pairs supplied to its RegionEdit. Unlike the previous c1
classification, it does **not** reason from finitely many winding samples.
It independently rediscovers **every** V13 contact involving each changed
ORIGINAL source face, with bounded BVH, triangle, and reported-hit work,
and demands exact equality with the proposed contact pair set. Any omitted
original contact, stale/duplicate pair, ambiguity, unsupported tangent,
grazing/coplanar geometry or budget exhaustion refuses the entire result.

Every declared pair is independently re-evaluated using the original
transverse section extractor. The section's permanent cut endpoint IDs
must resolve to both of the same original geometric endpoints while
remaining topologically DISTINCT on the two sheets. On EACH sheet,
the entire original intersection segment must be covered, without
missing intervals, by collinear edges of the ACTUAL child FaceIds.
A curve broken at a crossing or interior T-junction is naturally accepted
as a chain of multiple edges; an uncut diagonal through the open
interior does not count. To distinguish true open-interior interference
from harmless crossings of an opposing face's infinite supporting
plane OUTSIDE the actual triangle, the verifier clips the child's
intersection with that plane to the FINITE original transverse
contact interval. A positive-length overlap in the open interior
refuses, whereas an intersection beyond the opposing face does not.
Within the supported finite-tolerance predicate domain, this excludes
a remaining unmodeled child-interior contact. Geometry
is checked independently: children must remain oriented, coplanar,
inside the source boundary and sum to the original signed area; child
triangles of the same source must not overlap each other. RegionEdit
has already validated edge incidence and linked manifold fans.

**Scope of the guarantee:** The complete set of supported V13 original
triangle intersections is checked for the entire affected source halo.
For transverse noncoplanar pairs, the intersection is a straight line
segment. By verifying that the *whole* segment is on child edges and
that none of those children straddles the opposing plane, the verifier
rules out an unmodeled CROSSING through their open interiors; finite
winding probes are not treated as proof of this property. Claims apply
only to the solver's finite-tolerance predicate domain: very close,
grazing, degenerate, ill-conditioned, coplanar or unresolved geometry
must fail closed. The certificate returns no partial covered-contact
set on refusal, performs no deletion or publication, and neither
classifies cavities nor selects which faces should be removed.
Regression includes all nine original contacts from a full closed
two-tetrahedron source pair and both complete mirrored groups with
dyadic local coordinates, plus selectively omitted contacts, forged
permanent endpoints, child edge and collision budget exhaustion, and
stable historical roots. Unsupported translated-decimal configurations
remain fail-closed rather than being mislabeled as complete.
A subsequent stage must combine certified contact coverage with c1
reliable positive-signed-winding occupancy on EACH real child and
explicit material/cavity policy, then run private V15 excision, V16
closure, full residual/fidelity auditing, and final Step-wide CAS.
The desktop Merge geometry option remains disabled.

## V17e2b2c1: Bounded two-sided winding on real subdivision children

The new assessment operates on actual child FaceIds in the unpublished
V17e2b2b1/b3b `ConformingCutCandidate`, not artificial V14 barycentric
subpatches of a whole partially intersected original triangle. Each
retained/replaced and inserted child carries explicit `(child, original)`
FaceId provenance from the creating RegionEdit; the exact immutable
original and private candidate index roots are held for independent
verification. Missing, contradictory or forged provenance, children
outside their original plane/boundary, inverted small triangles, or failure
of the sum of all oriented child areas to equal the original area refuse
the entire classification batch.

For each actual child triangle, take its center and three additional
STRICTLY INTERIOR barycentric witnesses. On each side of its oriented
plane, query at two positive offset distances constrained by both the
ORIGINAL mesh edge length and current child edge scale. Accelerated rays
must provide reliable integer winding WITHOUT an exceptional O(all faces)
solid-angle fallback. The minus and plus winding must differ by +1,
remain identical across both distances and all four witnesses, and
classify occupied material only for `w>0`; `w=2` and `w=1` both
mean solid, `w=0` and negative winding mean void. For example, a
positively oriented nested internal shell has solid on BOTH sides and
is tentatively buried, whereas a real inward-oriented enclosed cavity
surface can still separate positive solid from zero-winding void and
must stay exposed. Unreliable rays, offset below floating-point resolution,
close surfaces, inconsistent interior samples or exhausted finite work
budgets refuse the entire result. No partial fragment labels escape.

Unlike the historical V14 query, both initial on-surface proximity and
actual ray BVH directory/partition/triangle visits now take hard ceilings
BEFORE each visit. The fragment batch tracks remaining aggregate box,
partition, triangle, ray and winding-point query budgets. The legacy
unbounded winding API remains available by default for non-live callers.
The new classifier is pure, never publishes or modifies a root, mirror
map, history, renderer or checkpoint, and returns NO removal authorization.

**Important mathematical boundary:** Sampling four interior witnesses of
a geometrically exact child face does NOT alone prove that no additional,
unmodeled intersection curve crosses an unsampled portion. Hence this
first standalone stage is an occupancy ASSESSMENT, not a certified exact
Boolean deletion oracle. V17e2b2c2 must prove bounded local contact-curve
COMPLETENESS and absence of any unmodeled child interior intersections
before safe exact-fragment excision can be considered. No V15 deletion,
V16 closure, live publication or UI activation is enabled.

## V17e2b2b3b: Persistent mirrored cut staging in one private RegionEdit

The completed read-only mirrored V17e2b2b3a planar arrangement (or the
older single-section reflected planner) is rebuilt against the exact
immutable baseline by `materializeTransverseCutCandidate`. The mirrored
path validates local original mirrored VertexId pairs with exact world
reflection and involution, reflected source faces with opposite cyclic
winding, complete mirrored endpoint and interior junction indices, and
every reflected contact curve. The local RegionEdit receives ONLY touched
original source-face corners and their reflected pairs, not a copy of
every persistent mirror pair in the model. It rejects invariant original
faces, new self-mirrored X=0 cut points and unmatched section/face orbits
before beginning the persistent allocation.

For each new pair of ephemeral cut points, appendMirroredVertices reserves
TWO distinct permanent VertexIds; the two independent crossing sheets are
NOT welded, even when their positions coincide. Each reflected original
face pair retains its original FaceIds in replaceMirroredFaces, while
appendMirroredFaces gives each additional canonical child and its
reflected/reverse-winding counterpart distinct never-reused FaceIds.
The canonical face's triangulation defines both sides, avoiding differences
between independent triangulation scaffolds and ear diagonals. The return
value includes all net inserted vertex/face IDs, all rewritten originals,
and explicit mirrored pairs for both inserted vertices and changed faces.
These are metadata for a FUTURE Step journal; no actual live mirror map,
renderer dirty list, checkpoint, undo history or repository CAS is changed.

One private RegionEdit::commit checks all changed edge incidences,
vertex-fan manifoldness and reflected face/vertex cycles and creates a
single immutable COW candidate root. Errors in pairing, source geometry,
work or writable partition capacity throw, discard the complete private
transaction and retain every old reader root. Stable IDs allocated in
discarded attempts remain non-recyclable by the underlying allocator.
Closed reflected three-tetrahedron crossing and T-junction fixtures and
ordinary symmetric single-section conformance test paired materialization,
exact positions/winding, stable originals, unpublished roots, residual
intersection and refusal rollback. This is **NOT** a Boolean union: the
subdivided shells still intersect. V17e2b2c must classify actual children
by strictly positive signed winding on both sides before V15 can
authorize fragment removal; V16 closure and full collision/fidelity
auditing must then precede a single live Step publication. Unsupported
X=0 cut/seam topology continues to fail closed and Merge geometry
remains disabled.

## V17e2b2b3a: Explicit reflected planar-arrangement orbits (private)

A completed V17e2b2b2 multiple-section graph may now be paired against
an explicit, involutive original VertexId mirror map. The preflight is
restricted to touched source faces, their incident edge neighbors and
their generated cut points; it never scans the entire model. Each original
mirror vertex must exist, be exact X-reflection in world coordinates and
form a reciprocal orbit. Every affected original FaceId requires a unique
reflected original face with REVERSED winding. Edge-cut points match by
mirrored original Edge IDs; interior endpoints and newly inserted proper
crossing/T-junction graph nodes match by mirrored owning FaceId, within
an original-edge-scale numerical tolerance. Those checks explicitly
prevent accidental welding of distinct colliding shells. Any missing,
duplicated, non-involutive or ambiguous point/face/contact-curve orbit,
or insufficient bounded pairing work, refuses the entire read-only plan.

The two independently calculated arrangements can choose different ear
diagonals and noncontact scaffolds. Following validation of complete
point and section orbits, the mirrored child triangles are therefore
generated by reflecting the canonical side's triangles and reversing
winding. All resulting children must again conserve their own original
face's area with strictly positive orientation; each new point must
remain on its own original edge or source face plane. Pairing work has
its own finite limit, separate from intersection and ear predicates.

This deliberately refuses new self-reflected X=0 cut orbits and
invariant original source faces; a shared seam fan cannot be justified
by a world-coordinate coincidence. Ordinary pre-existing X=0 vertex
identities are permitted only with exact involution and X=0 position.
Closed reflected three-tetrahedron tests cover real interior crossings
and interior T-junctions, exact point/triangle orbits, lost mirror map,
missing reflected contact and work exhaustion. This stage is ONLY
an ephemeral plan: no RegionEdit stable-ID allocation or staged mirror
metadata, no published revision and no union. V17e2b2b3b must pair
permanent VertexIds and FaceIds in one private manifold-validated
RegionEdit before any V14 fragment occupancy/removal work.

## V17e2b2b2: Multiple transverse sections per source face

When an original FaceId participates in multiple distinct certified V13
contacts, the V17e2b2a fast path delegates to the bounded read-only
multi-section planar arrangement. Each local contact contributes two
original-face-constrained endpoints; original mesh EDGE intersections
are deduplicated and reused across all incident triangles, while
independent intersecting sheets never share their cut VertexId identity.
Within EACH source face, all segment pairs are checked for proper
crossings and meeting endpoints. They are split at shared 2D junctions
in a consistent original-face coordinate frame and inserted into one
undirected embedded graph together with the full subdivided perimeter.
Internal degree-one constrained endpoints are completed with carefully
visibility-tested noncrossing ordinary triangulation scaffold edges;
these do not authorize Boolean surgery. Directed halfedge walks extract
the positively wound bounded faces, which are ear-clipped into
nonoverlapping oriented triangles. The resulting faces preserve their
original FaceIds for first children and are materialized with all
incident edge-neighbor subdivisions via the existing ONE unpublished
V17e2b2b1 RegionEdit. The ordinary single-section path is unchanged.

Limits apply to input collision pairs, source/neighbor face count,
all newly created points, total output triangles, pairwise section
intersection tests, arrangement graph edges, and scaffold-visibility/
ear-clipping predicate tests. Coplanar/collinear
overlapping constraints, touching original corners, uncertain point
coincidences, dangling non-simple face walks and degenerate triangulations
refuse the *whole* arrangement without exposing partial cut topology.
The RegionEdit still performs its independent closed manifold
edge-orientation and vertex-fan checks before producing any COW root.
Three closed intersecting tetrahedra exercise proper crossings and
interior T-junctions; the tests preserve the original immutable index
and confirm collisions remain present until actual fragment removal.

**Scope:** This is a planar subdivision algorithm, NOT a successful
Boolean union or publishable Merge geometry Step. Mirrored multiple
sections are conservatively refused until their generated point and
child FaceIds can be journaled as a complete symmetric RegionEdit pair
transaction. Classification of split fragments by V14 positive winding,
V15 authorized excision, V16 cap/zipper and complete residual-collision
and anchored-feature auditing remain necessary. The desktop Merge
geometry option stays off and the V17 parent stays open.

## V17e2b2b1: Transactional conforming cut materialization, no union

`materializeTransverseCutCandidate` builds the V17e2b2a bounded
graph fresh from the current immutable index and caller-specified V13
contact pairs. It never accepts a caller-forged plan. Given supported
closed orientable input, it stages every affected original source face
AND its shared-edge neighbors within one private `RegionEdit`, creates
new globally unique VertexIds for the graph's new points, replaces the
first triangle of each changed original FaceId in place, and appends
remaining oriented child triangles with brand-new FaceIds. The
intersection endpoints retain distinct per-sheet VertexIds even where
their 3D positions coincide. The candidate reports the original IDs,
the new IDs, paired section endpoints and the single immutable new
mesh/index root. No repository publication, undo entry or render upload
occurs here; existing reader pointers and all old partition/index
objects remain valid.

`RegionEdit::commit` performs its existing full local orientable
two-manifold edge/fan checks before creating the new COW revision.
Uncertifiable, open, nonmanifold, ambiguous or over-budget cases throw
without exposing partially materialized revisions. A closed pair of
transversely intersecting tetrahedra is tested, including actual V13
intersection detection, one private commit, source-ID preservation,
separate contact-sheet vertex identities and refusal/rollback.
**Residual collisions are intentionally present:** this step merely
conforms tessellations. It does not remove a single fragment and cannot
serve as a valid live sculpt merge by itself.

Mirrored materialization is explicitly rejected until a complete
paired-face RegionEdit journal can allocate/replace each reflected
original/child face and propagate its mirror IDs alongside the single
Step history. Multiple independent crossing curves within one face
remain rejected by the V17e2b2a planner and require a bounded 2D face
arrangement in V17e2b2b2. Thereafter the exact fragments need
V14 positive-winding classification, V15 authorized excision, V16
closure and all final collision/feature-fidelity gates before any
publication. The desktop Merge geometry control stays disabled.

## V17e2b2a: Shared-edge conforming private patch graph

The read-only `buildConformingCutPlan` takes **bounded V13 collision
pairs** and V17e2b1's transverse sections. Each cut point on an existing
mesh edge is keyed to that original immutable `Edge{minId,maxId}`
plus its geometric fraction, with strict agreement for repeated
registrations. Its one ephemeral node index is reused in EVERY original
face incident to that edge; all such neighboring survivors join the
affected-face set, even if they never participated in collision discovery.
Faces are independently subdivided using the directed arclength order
of their boundary cuts, and every resulting triangle must have positive
original winding and conserve its original face's signed area. The
two independent intersecting shell surfaces, however, keep **separate**
node identities at coincident 3D points; the plan records the paired
curve endpoints as correspondence, not premature topology welds.

Optional strict reflection pairing derives mirror identities from the
*original edge or original owning face*, rather than treating any two
coincident points across the intersecting sheets as one. Reflected cut points must match within a strict bound derived from the
original local edge lengths and floating-coordinate roundoff; accepted
pairs are canonicalized to **exact** reflected positions. The resulting
points must still lie on their original source edge/face planes and all
triangulated patches must preserve original winding/area. Ambiguous or
missing reciprocal partners refuse the entire plan. Hard limits apply
to discovered pairs, affected original faces, newly generated points,
and total generated triangles. Unsupported/ambiguous contact, repeated
independent curve sections involving one source face, more than two
incident faces on a cut edge, or a new cut crossing an already
subdivided source face cannot silently be accepted. Either a complete
read-only result is produced or a refusal with **no partial topology**.
This is intentionally O(local involved faces), not a global mesh scan.

This milestone does NOT allocate persistent VertexIds or FaceIds and
does NOT perform a `RegionEdit`, publish a revision, classify new
V14 fragments, authorize V15 deletion or reconstruct the V16 scar.
V17e2b2b must handle multiple crossing curves per face with a
bounded polygonal arrangement, stage one conforming COW transaction
with fully checked geometry and mirrored ID deltas, classify each
exact fragment, perform closed private repair and prove repeated
physical tower unions. The live Merge geometry control remains off.

## V17e2b1: Read-only intersection sections before conforming topology

A newly separate, bounded CPU geometry primitive
`splitTransverseTriangleContact` takes two independent oriented V13
triangle surfaces and stable face-corner IDs. It first uses the V13
collision predicate, then extracts one COMMON, finite non-coplanar
intersection segment by clipping both triangles against the other's
supporting plane. The same two 3D endpoint coordinates are then validated
against BOTH triangle planes and stored with independent barycentric
coordinates. Each source triangle is triangulated *in memory* with that
intersection segment a genuine internal edge. Boundary-to-boundary cuts,
one interior endpoint, or two interior endpoints are supported, and all
subtriangles must preserve winding and the exact original signed area
within a bounded numerical tolerance. A finite `maxPairs` guards the
read-only V13-indexed batch: any unsupported member invalidates the
entire return set; over-budget work throws without partial output.
Original revision roots, vertex IDs and face IDs never change.

**Important restriction:** Two-face local section triangulation is NOT
a conforming topology commit. Intersection curves generally cross existing
edges shared by a *third* surviving face, and cutting one triangle requires
staging the same exact edge vertex in all incident faces, plus mirrored
matching IDs on the reflected surface. The section vertices here have only
*local* indices 3 and 4, no globally allocated VertexIds. Coplanar
overlaps, tangencies, corner cuts, nearly parallel planes, and uncertain
or too-degenerate sections deliberately return `UnsupportedContact`.
V17e2b2 must build a bounded edge-conformance graph and a candidate
`RegionEdit` before the fragment-level exposed/buried classifier can
authorize any excision and V16 can verify it. Do not interpret this
milestone as successfully merging the physically contacting tower fixtures:
their existing V16 residual-collision blocker persists and the Merge
geometry switch stays disabled.

## V17e2a: Anchored original-surface refusal and physical merger diagnosis

The CPU-opt-in candidate evaluator now accepts a deliberately selected
`SculptAnchoredSurfaceGuard` with (1) fixed witness points sampled from an
**original** exposed patch, (2) explicit surviving candidate FaceIds whose
triangles are permitted to represent that feature, (3) an original edge
scale, (4) a maximum normalized distance and (5) a hard upper bound on
witness-triangle comparisons. The guard uses V16's existing bounded
`auditSurfaceWitnesses` and runs **before** a Step's only publication,
for both no-contact and completely repaired candidates. A witness that
exceeds tolerance, a reference FaceId that retired, or an exhausted work
budget refuses the entire Step under V17c. It never silently falls back
to a full-mesh search or treats consecutive snapshot drift as the original
reference. Because intentionally sculpted features are allowed to move,
clients must explicitly nominate ONLY features that should stay fixed;
there is no default global guard or implicitly frozen surface.

The regression takes the original witnesses on one cube exterior patch,
computes two successively displaced private candidates, chooses a guard
threshold STRICTLY between their measured deviations from the original,
accepts the first and rejects the second in the actual published brush.
Zero-cost initial geometry, deliberate stronger erosion, exact work cap,
unchanged published roots and empty dirty updates are independently tested.
V17e1's sixteen symmetric two-tower Steps now also run with explicitly
anchored unchanged outer-wall witnesses, supplementing their exhaustive
external seven-sample audit of original geometry after every Step.

**Physical union diagnosis:** A deliberately failing V17e2 probe required
successful unions from real symmetric tower contact. It revealed that
at least one proposed V15-classified private merger enters V16 reconstruction
but is refused by the residual checker because newly constructed surface
triangles intersect surviving faces. Such an attempted reconstruction
MUST remain rejected; reducing the V15 hidden-triangle threshold or simply
ignoring V16 residual contacts would violate the publication contract.
The permanent acceptance suite distinguishes a fully successful union
from this explicit reconstruction-stage blocker and retains immutable
baseline/undo/witness checks. The requested *repeated successful physical
union* acceptance is **not** claimed complete: V17e2b requires a supported
and verified intersection-curve subdivision or an independently certified
repeat-contact fixture which genuinely yields successive classified unions.

## V17e1: Repeat-contact safety acceptance, not a merger guarantee

The optional CPU-only V17d2 path now has a deterministic **physical**
contact stress fixture in the repair-surgery CPU suite. Two distinct
mirrored cube-tower pairs begin separated from X=0; the normal-directed
symmetric Inflate sample is checked independently to confirm its actual
post-state faces collide. Sixteen successive brush Steps alternate the
real towers, keeping all historical reader roots alive. At each Step:
(1) V13/V14 are run rather than cached, (2) a refused candidate publishes
no root or undo entry and preserves the mirror map/dirty render set, or an
accepted candidate must have zero contacts across EVERY active face in
the complete output, and (3) the original outer walls retain their sampled
position fidelity. Fifty-six fixed original exposed-surface witnesses are
read from the **initial** revision and re-audited after EVERY attempt;
the distance test is explicitly bounded for these small fixtures. This
complements V16's separate thirty-two-generation *same-patch* cap/zipper
fidelity regression, which anchors and checks the original capped patch
rather than shifting its reference to the previous revision.

For diagnostics only, `SculptContactTracker` tracks the number of
contact episodes, contacts, genuinely clean checks and unknown/overbudget
checks. A configurable default of three consecutive **conclusive clean**
queries ends an active episode; uncertain queries cannot end contact.
The worker exposes those counters. This is deliberately NOT a repair
cache: every proposed changed Step still runs the complete V13-V16
validation, with identical refusal/commit behavior as before. History
undo/redo may change geometry while retaining the diagnostic counters;
they must never be treated as authoritative per-revision topology state.

**Critical acceptance distinction:** The regression proves safety at
physical repeated contact, NOT reliable **successful merging** of those
contacts. V15's whole-triangle approximate hidden-fragment classification
can conservatively reject symmetric folds/tangencies, including all
attempts in a particular stress run. The model remains valid on refusal,
but that is not sufficient for the eventual interactive Merge geometry
experience. More V17 work is required on actual successful repeat unions,
ongoing original-surface witness policies that permit intentional edits,
bounded aggregate per-Step work, and post-publication strong exception
atomicity. Do not enable the desktop Merge geometry control on the basis
of this acceptance harness alone.

## V17d2: Atomic mirror correspondence for classified surgery

The V17d1 CPU-only opt-in now also supports classification-approved
symmetric excisions where V16's private mirrored cap/zipper (and its
qualified shared-plane crossing normalization) succeeds. The V15 plan
must arise from the **same complete post-Inflate/refine/collapse candidate**
being examined for the original Step. No caller-provided low-level contour
plan alone can authorize deletion. Strict positive-winding classification,
finite BVH queries and exposed-surface fragment checks remain mandatory.
All seam-normalization revisions remain private and are folded into the
final single original-root publication. Their intermediate dead IDs do
not inflate net vertex/face counts; cumulative COW partition statistics
still include their actual work.

The V17c post-collapse live mirror map describes the candidate's living
IDs. Only when the V15 classifier finds a Ready plan is that map converted
to V16's canonical mirror-pair vector. For each new final vertex in the
scar and shared X=0 seam, V17d2 demands a unique vertex at the **exact**
reflected coordinates. Vertices on X=0 map to themselves; off-plane ones
must have their distinct reciprocal twin. New vertex IDs cannot already
exist in the original candidate. For each retired vertex, V17d2 requires
its mapped partner to be retired as well, with reciprocal mapping; a
partly retired orbit refuses the Step. Only these touched pairs are
staged in refiner/collapser reversible delta journals. On a V17c veto or
repair/verification refusal, the original root, the live Inflate map,
checkpoint, history cursor and dirty partition set stay unchanged. On
success, the new/retired pairs join the ordinary refine/collapse pair
deltas in the same history transition and Inflate update. History
normalizes transient pairs created and retired within one Step.

The entire changed candidate including normalization survivors is checked
against the complete repaired revision with finite V13 budgets before
publication. Success still produces ONE repository CAS, ONE history entry
and one coalescible renderer delta. Tests include a mirrored redundant
component excision on a real symmetric stamp, prior validator refusal,
subsequent acceptance, undo/redo, durable binary checkpoint roundtrip,
exact X=0 seam mirror pairs, and incomplete-orbit refusal.

**Scope and limits:** This is not yet an assurance that arbitrary
intersection geometry is repairable or that multiple live symmetric
towers can be merged through V15's approximate triangle-only classifier.
Explicit isolated-component seeds are supported for CPU callers that
already know them; the worker does NOT search disconnected components
mesh-wide by default, nor activate any UI setting. Repeated physical
contact tests, sampled witness erosion policy, contact hysteresis,
aggregate per-Step budgets and strengthened post-CAS failure atomicity
remain V17 work. Unsupported reflection topologies and ambiguous
classifications must remain fail closed.

## V17d1: Classified candidate repair at the single-Step publication boundary

Opt-in CPU clients can enable `SculptCollisionSettings::enabled` for the
`VersionedSculptBrush` or `AsyncSculptWorker`. The desktop Merge geometry
option is intentionally **not exposed**. The current application defaults
to the V17c behavior and never performs new collision processing.

For each changed Step the policy works on the **entire, unpublished** result
of Inflate, local split/flip, and collapse. It invokes V15's
classification-aware planner and V16's private surgery via
`applyClassifiedExcisionRepair`, choosing the explicit
`PreserveSampledExposedFeatures` policy. V13 BVH collision discovery and
V16 residual surgery verification have finite hard ceilings (max 2,048
seed faces, 131,072 box visits, 16,384 partition visits, 131,072 candidate
triangles, 65,536 distinct narrow-phase tests, and 2,048 reported contacts
per query); smaller caller budgets prevail. V14's exceptional O(all faces)
reference winding fallback is forbidden. A rejected or over-budget plan,
failed surgery or ambiguous final contact vetoes the entire Step and uses
the V17c rollback and diagnostics path rather than terminating the worker.

A repair is not accepted just because the scar itself is locally clean.
The last query includes the original moved and rewritten faces, the entire
set of surgery-created faces, preserved neighboring contour faces, plus
retired IDs (which V13 ignores safely), all tested against the COMPLETE
post-surgery immutable revision. Successful non-mirrored surgery replaces
the root proposed for the **single** existing publish CAS and updates dirty
render partitions, surviving vertex/face counts, copied-payload accounting
and the one history entry. The V17c validation callback, if supplied,
receives that final repaired candidate. All intermediate revisions stay
private and old readers continue referring to the unchanged baseline.

**Mirrored caveat:** ordinary symmetric Steps with NO detected collisions
can pass this optional gate. The V15 classifier is run for colliding
symmetric cases, but V17d1 refuses any symmetric repair requirement:
V16's mirrored surgeon does not by itself supply the new/retired mirror-ID
deltas needed by the brush/refiner/collapser/checkpoint history. Publishing
its geometry without those deltas would corrupt symmetry. The separate
V17d2 gate will supply transactional mirrored classification, plane-crossing
normalization, and ID-delta staging, with genuine repeated-contact tests.
No claims of general exact boolean trimming, global two-sided Hausdorff
preservation, swept-volume CCD or hard aggregate per-stroke timing follow
from V17d1. Isolated-component seeds are only accepted explicitly by the
private test/evaluator; the normal worker does NOT enumerate disconnected
shells mesh-wide on each stamp. Existing asymmetric private V16 capability
remains conservatively bounded by its geometry/refusal rules.

## V17c: Reversible speculative Step with recoverable refusal

The live V8 worker still samples the exact 4D stroke. V17c adds an **opt-in**
`SculptStepValidator` invoked on one complete private post-Inflate,
post-refine, post-collapse `ProposedSculptStep` containing the original
published revision, proposed candidate and the exact changed vertex/face
IDs. Returning an empty reason accepts; returning a message refuses. A
validator's `std::length_error` (work budget) or `std::logic_error`
(unsupported geometry) is converted to a recoverable refusal. Other
unexpected failures remain fatal and are never classified as safe geometry.

Refiner and collapser preserve the V5-V12 **O(touched pairs)** mirror cost:
each Step records only mirror additions/deletions in a reversible journal.
Rejected candidates replay inverse mirror operations in reverse order;
private COW revision roots are discarded with no published root exchange,
undo entry, renderer dirty delta, checkpoint change, or accepted-work
accounting. The rejected Step is nevertheless counted as *consumed* by the
unchanged sampler so later queued samples retain their precise 4D phase.
The worker reports `rejectedStamps` and `lastRejection`. A worker may
publish a same-root status mailbox notification with an empty dirty set;
that is not a mesh-repository CAS. An accepted Step still publishes at most
one final index root and updates its ordinary history/dirty bookkeeping.

Regression includes an actual symmetric local split that is refused, then
replayed and accepted with exact checkpoint mirror-map preservation and
undo/redo; also a worker whose initial two samples refuse (one diagnostic,
one thrown work limit), but whose later samples drain and commit normally.

**Limits for future V17 milestones:** the validator has not yet been wired
to V13/V14/V15/V16 collision repair, original-witness tolerance or symmetry-
classified excision, and the default worker continues unmodified geometry
behavior. Prepublication rejection is transactional; abnormal failures
*after* acceptance (e.g. an unexpected history/CAS error) still need a
stronger prepare/commit protocol if they are to be treated as recoverable.

## V17b: Bounded collision discovery without partial-safe results

V13 `RegionTopologyIndex::triangleIntersections` now accepts an optional
`CollisionQueryLimits` for **each complete query**: maximum distinct seed
faces (including faces expanded from dirty vertices), AABB box tests across
the persistent directory and per-partition BVHs, partition visits,
overlapping triangle candidate visits, distinct narrow-phase pair tests,
and returned intersecting/ambiguous contacts. Counters report the real work.
Check each budget BEFORE increment or narrow-phase action; a limit of zero
forbids that work. At exhaustion throw `std::length_error` rather than
returning a truncated list that could be confused with a clean surface.
The historical no-limits default preserves current offline callers. A
future live worker must supply finite budgets explicitly.

`RevisionPickingIndex::forEachOverlappingTriangle` accepts these limits
and raises a bounded-work exception *inside* the visit, so the caller
cannot discover the excess after traversing the whole mesh. V15's
`planApproximateExcision` returns `BudgetExceeded` when its initial V13
contact search or V14 isolated-component contact check exhausts its
limits, with initial V13 work counters exposed in `RepairPlan`. The V16
private scar validator also forwards an optional distinct residual-
collision query budget and refuses without publication if it is exhausted.

Regression tests compare complete and exact-limit query results, decrement
one of six budgets at a time to exercise each refusal, and verify a
zero-budget empty query, a bounded collision-aware removal plan and
buried component planning, plus private residual verification with intact
immutable source roots. This does not make arbitrary intersecting geometry
repairable. **Scope:** each query is bounded individually, not all V14
component queries over an entire Step; the V17 worker still needs aggregate
work/time policies, bounding of winding *ray* traversal, conservative
per-Step rollback, correct symmetry-aware classified excision, and full
post-surgery collision revalidation before activating Merge geometry.

## V17a: Optional fail-closed avoidance of whole-revision winding fallback

The V14 positive-winding point query ordinarily tries up to three accelerated
independent rays, requiring two agreeing reliable ray counts. Previously, if
the rays remained inconclusive, it could invoke the expensive independent
O(all faces) solid-angle reference. V17a exposes a **caller-selected**
`allowReferenceFallback=false` option: that exceptional traversal is
suppressed and the query returns `reliable=false`. Statistics distinguish
`referenceFallbacks` (executed scans) from `referenceFallbackRefusals`
(strict-mode refusals). Normal ray classification still works with the same
positive-winding semantics; existing callers default to the old behavior.

The same switch propagates to `classifyFaceFragments`,
`classifyClosedComponent`, V15 `RepairPlanSettings::allowReferenceWindingFallback`,
and V16 sampled exposed-feature provenance. An uncertain fragment or closed
component may not authorize a private deletion. Tests use rays grazing three
explicit triangle boundaries to exercise the actual fallback refusal, plus
ordinary sphere, redundant-bubble and exposed-cap cases.

**Scope limit:** This is a first V17 prerequisite, not live Merge geometry.
It does not yet bound pathological accelerated BVH/ray traversal, V13
collision candidate enumeration, all optional provenance work, or worker
wall-clock time. V17 must select strict mode, provide the other budgets,
and integrate fully staged repair/rejection at the Step publication boundary.

## V16 Final Acceptance Review — qualified private foundation

The completed V16a–V16d2b fixtures and cross-platform CI support
**acceptance of the PRIVATE, transactionally validated surface-repair
foundation**, with a strictly limited geometry subset and fail-closed
behavior. The per-operation, per-revision success tests do NOT constitute
live brush activation, universal intersection reconstruction, automated
deletion authorization from low-level contour nominations, continuous
Hausdorff bounds, or a real-time performance guarantee.

The [V16 final acceptance matrix and V17 activation contract](
docs/V16_FINAL_ACCEPTANCE.md) identify the precise tested fixtures,
known geometric refusal cases and mandatory live-boundary checks.
In particular, V17 must supply a classified symmetry-aware caller,
check relevant residual collisions on the final full Step candidate,
budget exceptional whole-mesh winding and pathological discovery work,
handle rejected Steps without terminating the worker or skipping input,
and commit every mutable mirror/history/dirty-partition detail together
with **one** CAS from the original Step root. The original-anchored
surface-witness audit remains read-only until V17 chooses explicit
feature anchors and cumulative tolerance. V18 is separate nonmerge
collision-constrained displacement; V19 remains wider desktop acceptance.


## V17e2b2d4 — First bounded detached-loop full-contact union subset

The earlier V17e2b2b2 arrangement can subdivide selected proper crossings,
interior T-junctions and open section chains, but degree-one endpoint
scaffolding did NOT cover a disconnected, closed, entirely face-interior
intersection loop. Walking both the inner cycle and its outer triangle
would double-cover area and fail the original-face orientation/area audit;
a single bridge yields a weakly simple polygon with repeated vertices.

For each detached graph component that is STRICTLY a simple degree-two
closed cycle, the planner now searches a finite set of original-perimeter
connector candidates. It installs exactly TWO ordinary, visible,
noncrossing scaffold chords: distinct cycle nodes to distinct perimeter
nodes, with bounded candidate and visibility predicates, graph-edge
and triangulation work, deterministic distance/ID order. These chords
join the graph sufficiently for simple halfedge regions and oriented ear
clipping. They are NOT V13 contact sections and carry no weld authority.
Unsupported detached branching, nested or unconnectable cycles,
ambiguous crossings and exhausted budgets still refuse the whole plan.
The original face area, oriented winding, child-edge coverage, protected
corners, stable source IDs, immutable old root and final residual-collision
audits remain prerequisites, not inferred from scaffold success.

The new closed-shell fixture is one large tetrahedral shell with TWO
independent compact tetrahedral protrusions. Each smaller tetrahedron
has two vertices on either side of the large shell's common top face:
all four of its original triangles participate in finite transverse
contacts, producing EIGHT complete V13 source pairs. The two resulting
disjoint interior quadrilateral section loops pass c1 positive-winding
classification, independent c2 continuous two-sheet coverage, c3
cavity-safe child selection, d2 provenance-only edge welding and d3
one-original-root Step composition. The test-specific V15 contour
envelope is finite and explicitly chosen: preliminary shape settings
are relaxed only for the test, while the permissible hypothetical
zipper gap is STRICTER (0.1 original-edge units) to avoid matching
unrelated loops. The production V15 defaults are unchanged; the final
union introduces NO zipper/cap strip or coordinate-based nearby weld.
The resulting surface has ONE connected closed component from THREE
original closed components, with preserved original picking identity.
A matching explicit async worker FIFO regression checks one terminal
snapshot, one original-root CAS/history Step, stale request refusal,
undo/redo and durable stable-ID tombstones. It is not automatic desktop
sculpt activation.

A separate three-body crossing and interior T-junction fixture now
submits the COMPLETE original contact graph to the cut/winding/
coverage/removal/composed-Step probes rather than using only two
selected section pairs. Acceptance is either an independently proven
terminal closed union or an entirely nonpublishable refusal. This test
does NOT claim that triple-junction unions now succeed. Importantly,
an initial protrusion with one exposed vertex and three buried
vertices left an entire unchanged original face fully buried. The
seam adapter correctly refused its unproven cut boundary; deleting
that untouched face would require a new complete safe classification
and retirement policy. The following remain separately open:
three-body crossing/T-junction final unions, buried unchanged faces,
unequal edge densities, reflected multi-section, self-reflected X=0
seams, and repeated automatic 4D sculpt/union desktop acceptance.


## V17e2b2d4b — Fully certified three-solid cut arrangements, sparse seam refusal

The next end-to-end fixture exposed two separate limits. Historical
three-tetrahedron crossing and interior T-junction examples each have
21 original V13 contact pairs, including nontransverse/grazing cases:
the selected two-pair planar arrangements are real, but those whole
closed-shell geometries are NOT supported full-contact Boolean inputs.
Never skip the 19 other original pairs or relax their V13 refusal.

A new generic dyadic geometry uses THREE independently closed,
oriented tetrahedra and a COMPLETE original graph of TWELVE strictly
transverse triangle contact pairs. An additional deliberately
orthogonal dyadic three-plane arrangement has SIXTEEN genuine
transverse original pairs after removing unrelated edge coincidences.
Both have contact lines meeting on an original source face. A detached
source-face contact graph may have real degree-three/four interior
junctions rather than only the degree-two closed cycles supported in
the first d4 subset. The bounded planar planner now attempts two
noncrossing, visible original-perimeter scaffold chords for such
detached components whose nodes all have degree >= 2. Neither chord
becomes a V13 seam or a weld key. Any weakly simple halfedge cycle,
nonpositive child, overlapping triangulation, broken edge fan, missing
contact, unsupported component or work exhaustion still refuses
atomically. In the NEW full-contact regressions, private persistent-ID
cut materialization and independently recomputed c2 complete real
child-EDGE coverage, c1 reliable strictly positive-winding occupancy,
and c3 cavity-protected exact-child removal all PASS. The original
historical readers remain pointer-stable.

A Boolean union involving three solids may bury PART of a true V13
contact section inside the third solid. The d2 seam tracer therefore
collects only c3-authorized directed surviving edge subintervals:
gaps and wholly buried sections are permitted, but overlapping or
duplicate intervals are forbidden. Each surviving subinterval must
still have an EXACT endpoint- and subdivision-matched partner with
opposite direction on the other original sheet. Every surviving
removal-boundary edge must be accounted for exactly once; no unrelated
colocated vertices become equivalences. The last RegionEdit manifold,
fidelity and whole-halo intersection audits, d3 original-root CAS and
worker/history acceptance are unchanged. Refusal reports the precise
contact and interval at 17 significant digits when there is a
subdivision mismatch.

Neither triple fixture currently produces a publishable union. Both
progress through c1/c2/c3 but refuse exact d2 seam matching because
independently arranged triple-junction vertices yield numerically
different section subdivisions/coordinates. The intended next bounded
task is *canonical, source-proven shared triple-plane junction point
construction across all affected original faces*, with a physical
exact-coordinate/area/fidelity and manifold proof; moving protected
corners or accepting near-neighbor matches by tolerance is NOT an
acceptable substitute. The original-root/worker/UI status is exactly
as before: no automatic sculpt Boolean, no UI Merge geometry, no
claim of completed three-body crossing/T-junction union. V17e2b2d4
and its V17 parent remain OPEN.

## V17-M01 — Canonical three-plane source provenance (flat-milestone acceptance)

The previously independent per-face 2D line intersection calculated
numerically close, but not exactly identical, cut junction points on
different intersecting source sheets. The final d2 provenance join
must not silently snap *nearby* vertices. This M01 solution acts
EARLIER on the unpublished original-source arrangement and uses exactly
THREE distinct FaceIds with all THREE genuine V13 finite source
contact sections. For every original face-pair triangle of contacts,
compute the common source-plane point from the three original
normalized plane normals using a bounded, non-near-singular 3×3
intersection. Independently verify finite point-in-section membership
on ALL three V13 segments, rejecting uncertain endpoint grazes.
No arbitrary coordinate hash, unrelated sheets or projected near weld
can authorize a canonical source junction.

An explicit ConformingCutJunction in ConformingCutPlan retains the three
original FaceIds, the one physical canonical position and THREE
topologically separate ephemeral point indices. At each source face,
the ordinary face-local crossing arrangement uses that common point
only for the exact source FaceId triple, checks it against independently
computed 2D line intersection, keeps originally existing/edge points
immutable and verifies a complete locally oriented fan. All face
area/orientation, multi-section planar triangulation, c1 bounded
positive-winding and c2 continuous original-section coverage checks
still apply to the materialized private candidate. Face/edge cases
that cannot preserve provenance and quality refuse rather than moving
original detail.

For order-independent original-contact enumeration, the provisional
graph is constructed in a stable FaceId-pair order; its returned
ConformingCutSegment array is then restored to the caller's input
order, which independently indexes original V13 sourceContacts and
materialized sectionFirst/sectionSecond. The per-face source-section
traversal is separately sorted by its original partner FaceId.
The hard maxJunctionCandidates budget rejects before publishing any
usable plan; the strict original-plane determinant and section
predicates reject unsupported near-parallel and grazing configurations.

The complete generic 12-contact and orthogonal 16-contact
three-tetrahedron regressions exercise exact THREE-sheet coordinate
identity, distinct provenance point indices, all three independent
source pairs, source-plane containment, at least three incident cut
children, reversed contact ordering and budget exhaustion. Both
remain fully certified by c1/c2/c3 after private real FaceId/VertexId
materialization. Notably the generic 12-contact three-body
fixture now also satisfies unchanged d2 exact provenance seam joining
and d3 3→1 closed-manifold Step acceptance. This is an opportunistic
positive regression, NOT a statement that M02/M04 are done: the
orthogonal full-three-body final union, unequal source subdivisions,
fully buried uncut source faces, reflected multi-section/X=0 orbits
and live sculpt-triggered repeated merging retain their separate
flat acceptance milestones.

No desktop toggle, automatic worker merge, new cap/zipper or
approximate protected-surface movement is introduced by M01.

## V17-M02 — Private certified unequal-seam subdivision and matching

A 3D intersection section's independently projected normalized
`double` arc-distance parameter is NOT an identity for welded geometry.
Two original-source paired vertices can have exactly equal XYZ but
slightly different projected arc parameters. The unchanged strict d2
join now uses source-contact provenance and **exact XYZ comparisons**
on BOTH interval ends, with mandatory opposite oriented survivors
and unique source section ownership; it does not introduce a tolerance-
based nearest-neighbor weld. Scalar parameters remain useful ONLY for
bounded ordering and potential interval-overlap preflight.

An additional explicit-only private
`reconcileCertifiedSeamSubdivisions` accepts a complete
`ConformingCutCandidate` already authorized by c1/c2/c3 against the
same immutable original cut. Its c3-directed boundary set is constructed
from surviving true child edges, retaining their original source FaceIds.
For every full original V13 section, it traces BOTH sheets, tests every
open survivor subinterval and refuses unilateral coverage; the gaps
buried inside a THIRD solid legitimately remain absent from BOTH
sheets. For every actual missing subdivision vertex, the exact physical
coordinate is borrowed ONLY from an already-real OPPOSITE sheet seam
vertex on the same source FaceId pair's finite section. A local
projection certifies the point lies strictly inside the relevant
source-child edge; near-but-nonexact endpoints, duplicates, ambiguous
source ownership and off-edge positions refuse atomically. Global
position matching is NEVER used.

All affected existing incident faces are privately split as one
conforming RegionEdit. New stable VertexId/FaceId allocations and
original FaceId provenance are journaled, with finite work,
new-vertex and new-face caps. Every existing source corner stays
unchanged. Original child orientation and total oriented area are
revalidated before committing the extra private COW root; c1 signed
winding/c2 full original V13 child-edge coverage/c3 cavity-safe removal
are RECOMPUTED on every new child FaceId. Finally each full contact's
surviving section intervals must have equal subdivision counts,
exact XYZ endpoints, opposite direction, no duplicate ownership and
NO unattributed survivor boundary edge. Any failed gate returns no
refined root or journal and leaves the baseline, pre-refinement
candidate, renderer and history untouched. New mirrored split orbits
fail closed until V17-M05.

A targeted regression deliberately subdivides one existing, fully
certified seam edge on ONE sheet of the generic 12-contact three-tetra
private conforming cut; the old direct d2 join correctly refuses its
asymmetry. The adapter inserts the required extra seam vertex in
the opposing sheet, splits both faces incident to that source edge,
reaudits the complete source graph and c1/c2/c3, and permits the
existing d2 unmodified geometry logic to close a genuine 3→1 manifold.
No-op exact matching, missing original contacts, insertion work cap,
mirrored missing orbit, old immutable roots, and exact final
provenance correspondence are tested.

The public `prepareExactCutStep` still consumes its existing
original-root conforming edit plus exact join. A newly refined cut's
additional intermediate COW commit is **PRIVATE only** here; it is
not yet composed into d3's one original-root history/dirty/tombstone
Step. V17-M04 owns that integration and full orthogonal three-body
positive union gate. M05/M06 own mirrored/fixed-plane extensions.
No automatic Merge geometry, V18 Off displacement constraint, V15
quality relaxation or approximate protected source position
changes are introduced.

## V17-M03 — Bounded untouched original-face retirement authorization

The earlier c1/c2/c3 child-deletion journal permits retirement only
of real subdivided children of an original transverse V13 contact.
A small closed tetrahedron with ONE vertex above another tetrahedron's
top surface and THREE vertices below that surface has a completely
buried base face which receives NO original transverse section and
therefore NO cut-child provenance. Attempting to force the baseline
c3 proposal to delete that old FaceId would bypass the complete
original-contact/source identity contract. Leaving the face behind
would create internal double-skin or prevent a true Boolean union.

`planUncutFaceRetirement` is an independent immutable READ-ONLY
evidence path, and never accepts a caller's original FaceId nomination
or a manually constructed `RepairPlan::Ready` contour. It first
RECOMPUTES c2 coverage for every supplied original V13 contact pair
and c1 complete cut-child signed occupancy against the SAME private
source revision. All child classification decisions are rechecked
for an exact +1 original oriented winding jump; Exposed and
ReversedBoundary are protected while Buried and Outside are eligible
for retirement.

The additional candidate search traverses the ENTIRE original
edge-connected component of untouched faces adjoining changed
source faces, stopping only at independently verified changed
original-face boundaries. Traversal is explicitly finite and
budgeted: exceeding any original-face or adjacency cap aborts the
whole proposal without a subset result. Every candidate still must
exist in both immutable indices with exactly the same original
FaceId, partition owner, ordered VertexIds and XYZ corner positions.
For EACH uncut face, an independently bounded V13 original-revision
triangle-intersection query must yield ZERO unresolved contacts.
This is the whole-original-face contact-free condition necessary
to upgrade local interior winding observations to homogeneous
classification on the supported nondegenerate closed surface
domain: an unreported section or even a potential grazing pair
must not slip through just because finitely many probes missed it.

As with c1, the winding probes use four strictly interior barycentric
positions and two normal offsets on each of BOTH sides. The face
must have one stable signed winding jump of +1 at all probes and a
single occupancy class; any disagreement or near-resolution
offset refuses. Queries use accelerated bounded work only:
no fallback to full-mesh solid angles. Buried/Outside originals
are authorized; Exposed and reversed true cavity boundaries
are always protected. The candidate faces are added to the
recomputed c1/c2-selected child FaceId removal set and a NEW
V15 contour-quality plan is required to match the FULL combined
FaceId set exactly. The proposal is otherwise never allowed to
mutate any old/private root, tombstone, picker, renderer or history.

The regression's real two closed tetrahedra yield three complete
transverse original contacts from the three sloped protrusion
sides. The fourth B face, its buried base, has NO V13 contact.
The baseline c3 child-only nomination cannot include that original
FaceId. M03 positively authorizes it, protects the large tetra's
untouched outer faces, and stages the complete deletion candidate.
An additional disconnected inward-oriented nested closed shell is
a separate true internal cavity: it cannot be reached from the
cut-source connected component and is NEVER nominated for removal.
Incomplete original contact pairs, zero face/adjacency/winding
budgets, V13 collision budget, nonconforming old IDs, uncertain
winding and unsupported mirror orbits refuse with NO partial
FaceId list or contour. Source and candidate picking roots remain
unchanged. Broad nontransverse, wholly coincident and arbitrarily
nested imported shell semantics remain outside this certified domain.

V17-M04 still owns actual safe deletion plus d2 exact section join
and ONE original-root CAS/undo/history/checkpoint Step. In particular,
M03 does not introduce a new intermediate published cut nor alter
the default disabled UI Merge geometry behavior. M05/M06 own
paired reflected and fixed-plane original-face deletion orbits.

## V17-M04 — Full three-solid original-root net union acceptance

The M01–M03 private geometric certificates now feed one ORIGINAL-root
V17 d3 Step. `prepareExactCutStep` internally stages an M01
conforming revision, passes its complete original V13 source pairs to
the bounded M02 exact seam interval/refinement preflight and uses the
resulting additional PRIVATE RegionEdit, if needed, as the final cut
input. M03 independently recomputes whole-face no-original-contact
and positive-winding c1/c2 evidence for every potentially buried
ORIGINAL face that received no section cut. The M02 trace stage and
the d2 terminal join are given exactly the same M03-authorized
untouched-original retirement set: a buried untouched base is not a
surviving seam edge simply because it remains in the intermediate
cut revision. This alignment is certified, NEVER inferred by spatial
proximity or permission to erase an arbitrary unchanged face.

All contact sections still come from COMPLETE rediscovered original
V13 FaceId pairs; unmatched one-sided intervals, duplicate ownership,
nonexact physical endpoints, wrongly directed seams, unsupported
reflection, uncertain occupancy, uncut face with a V13 contact,
nonmanifold/incomplete incident fans and budget overruns fail closed.
The final exact join retires only c1/c2/c3 children plus
independently M03-authorized untouched originals and welds only
exactly coincident, source-proven V13 edge endpoints with opposite
survivor directions. Final changed-halo and whole-final collision
regressions refuse any remaining nonadjacent surface intersection.
Every protected exposed or reversed-cavity child and original
retains exactly its previous physical location.

The d3 composition compares the FINAL postjoin immutable revision
ONLY against the ORIGINAL baseline (never the intermediate cut/
refined root). Stable IDs born-and-retired within unpublished edits
are excluded from live additions, while original IDs actually
retired—including M03-authorized previously unchanged original
FaceIds—enter net tombstone/undo/checkpoint journals. New and
rewritten FaceIds, inserted and retired VertexIds, mirrored history
(for legacy supported mirrored Steps), dirty partition owners and
actual COW payload-copy work from all private edits are composed
into a single terminal `RevisionCommit`. A consumer's ONE CAS
against the captured original root and ONE `RevisionHistory.append`
are the only possible acceptance/publication operations.

Positive integration regressions now require BOTH complete
12-contact generic and 16-contact orthogonal three-tetrahedron
graphs to form one connected, closed, oriented edge- and vertex-
manifold private 3→1 union. They audit ALL final live faces for
any nonadjacent intersections, zero protected surface displacement,
net original-root stable-ID/element-count changes, a successful
CAS plus stale-CAS replay refusal, exactly one undo/redo Step,
and durable checkpoint restoration with exact tombstones and
closed connected geometry. An additional one-above/three-below
protrusion retires the wholly buried, **uncut** original tetra base
while leaving an unrelated inverted enclosed cavity shell intact;
its two remaining connected closed surface components represent
the union exterior and protected internal cavity. Deliberately
incomplete 12/16-source graphs, tightened global net journal and
refinement predicate budgets and the historical complete 21-pair
grazing/T examples are negative all-or-nothing controls.

The cut, optional refinement, M03 classification, exact join,
terminal root and net journal are still PRIVATE until a caller
chooses the explicit worker operation. Symmetric multi-junction
refinement/untouched-source retirement remains gated until M05;
X=0 self-paired orbits remain M06. These tests do not enable
sculpt-driven repeated post-Inflate Boolean Steps (M08),
continuous cumulative fidelity/hysteresis (M09), per-physical-Step
aggregate resource caps (M10), injected post-CAS transaction
failure protection (M11), or the default-off Merge geometry
desktop switch (M12). Broader V19 imported and near-degenerate
universality is not claimed.

## V17-M05 — Complete off-plane mirrored three-solid Boolean acceptance

A full six-closed-tetra fixture consists of two independent EXACT
reflected three-tetra clusters centered away from X=0. The generic
half contains 12 strictly transverse original V13 contact pairs,
and the orthogonal half contains 16; their reflected copies give
complete 24- and 32-pair original graphs. They are NOT hand-selected
subsets of the historical 21-contact nontransverse fixtures. Both
halves now independently pass original finite-section conformance,
source-plane triple-junction construction, c1/c2/c3 signed-winding
removal, and exact d2 seam pairing/closure. The terminal result is
TWO distinct closed connected union surfaces, never a global
coordinate-key weld of unrelated shells. New original cut points
and source-owned FaceIds retain distinct IDs until proven physical
weld, and each separately reflected point/face orbit is staged
with opposite face winding.

The mirrored arrangement previously refused an otherwise certified
shared original endpoint when projected independent reflected 2D
section lines yielded t=u=1.0000000000000002. A harmless shared
endpoint is now recognized ONLY by an EXACT COMMON ORIGINAL CUT
POINT INDEX before recomputing its interior segment intersection.
Because two nonparallel straight original sections sharing their
actual endpoint cannot intersect again, this introduces no
approximate numerical weld. Parallel/coincident overlap remains
the former explicit refusal and two numerically close DIFFERENT
endpoint IDs still go through the unchanged strict near-graze test.

M03's previously asymmetric uncut-face authorization now admits an
explicit reciprocal historical mirror map, but only for distinct
off-plane vertices/unchanged original FaceIds. EVERY original face
in the complete source-adjacent untouched component must have ONE
reciprocal source partner identified through its original stable
VertexId triple. All three target vertex coordinates must reflect
EXACTLY, with opposite cyclic face winding; mirrored whole-face
positive-winding classification must agree for Buried/Outside
authorized original faces as well as Exposed/ReversedBoundary
protected original faces. Inconsistent or noninvolutive maps,
invariant X=0 points and self-partnered original faces all refuse.
Unlike approximate paired geometry, the exact original-face partner
is derived without global nearest-position search. A real
two-sided three-tetra mirrored fixture has two buried unchanged
base FaceIds and two separate inward-oriented cavity shells;
the two bases form ONE paired retirement orbit and BOTH cavity
shells remain permanent and protected.

The final d2 seam join now accepts those independently M03-
AUTHORIZED mirror-off-plane original retirement orbits while
preserving and rechecking untouched exposed/cavity original faces
even when the M03 decision reports NoChanges. A missing pair
or one-sided occupancy is refused before mutation. The terminal
M04 one-root composer still compares ONLY the old published
revision with the fully finished private joined revision, so net
stable original VertexId/FaceId retirement, live new paired ID
orbits, face rewrites, dirty partitions and tombstones occur in
ONE immutable root CAS/one history/undo transaction.
A tight original-face winding work cap, original V13 pair omission,
mirrored V13 point-pair budget, net permanent ID cap, wrong
historical VertexId involution/FaceId partner and dropped
mirrored child FaceId orbit all return a refusal without a
publishable terminal root or partial usable mirror delta.

Two targeted private fixture regressions verify ALL live old/new
mirrored vertex maps are involutive and positions EXACT, every
terminal FaceId has one distinct reverse-winding reflected FaceId
partner, the entire resulting mesh has no unexpected contacts,
ONE original-root CAS, atomic stale CAS rejection, a single
reversible history entry, and a durable checkpoint restoring both
the final geometry and retired original/new mirror orbits.
An explicit queued AsyncSculptWorker 24-contact test additionally
verifies a successful full multisection mirrored worker Step,
a rejected stale original request, one undo/redo returning both
original/terminal roots, and checkpoint reload preserving only
surviving exact reciprocal mirror pairs. The worker's three
controllers (Inflate, Refine, Collapse) already stage/commit the
same net delta; no new hidden mirror map is introduced.

Self-paired X=0 seams and invariant source faces remain a
separate V17-M06 obligation. Extra unequal-density paired
M02 subdivisions needing NEW reflected COW split orbits remain
fail-closed if not explicitly proven; this remains an explicit
supported-domain boundary for M07, never a license to
silently approximate or partially publish. Genuine automatic
post-Inflate/refine/collapse Merge geometry is still off and
belongs to M08/M12. Original V13/V15 thresholds, ring/zipper
heuristics, protected geometry, V18 displacement constraint
and broader V19 imported/large-model acceptance stay unchanged.

## V17-M06/M07 flat forward replanning — preserve original X=0 objectives

The previous single M06 combined two independent certification
layers: V13-to-private COW conforming source topology with
on-plane fixed-point identities, and terminal c1/c2/c3/d2
Boolean removal/welding with original-root atomic history.
A genuine complete three-pair closed-tetrahedron X=0 fixture
revealed independently checkable failure modes: unsupported
original tangency, about 5.55e-17 source-plane rounding,
asymmetric planar ear diagonals, original-edge conforming
midpoints, fixed-point allocation and mirrored seam class
ownership. These are NOT grounds for global nearest-coordinate
snapping or arbitrary Boolean deletion.

The SAME original requirements are now planned as two FLAT,
standalone green-CI milestones: M06 source-only certification
and M07 exact terminal union/history. Former M07–M12 now
M08–M13 respectively with every refusal-matrix, real
sculpt-triggered union, active fidelity/hysteresis,
aggregate work bound/telemetry, post-CAS fault protection
and default-off desktop acceptance retained in order.
No deeper child tasks are introduced and V17 stays open.

## V17-M06 — Fully conforming private X=0 original-source cut

The positive test now uses TWO closed reflected tetrahedra, each with
a pair of mirror-related original off-axis vertices and two original
self-mirrored X=0 vertices. A first geometry candidate had six V13
contact hits but THREE were genuine grazing/tangencies on a source
supporting plane, which the conservative V13 intersection-section
solver rightly rejected. Moving that one source vertex to an
interior dyadic coordinate left exactly THREE proper strictly
transverse original contact pairs. Source coordinates and the FULL
pair list, not selected subcontacts, are fed to the existing
conforming planner.

Two independent V13 triangle-section calculations on a proven
invariant source face/edge placed the SAME physically invariant
original-source cut point at X=-5.55112e-17 and X=0. This is
a roundoff artifact *after* the original mirror involution and
fixed source-face/edge provenance have been established. The
mirrored source normalizer now accepts the self-orbit only after
a bounded finite near-X-zero check AND an exact original source
ID proof. It canonicalizes ONLY the X coordinate; neither Y/Z
nor historic vertices change. A different point ID at nearby
coordinates is NEVER globally welded, and near-plane input with
no original invariant source proof continues to refuse.

An invariant original triangle has a unique original FaceId, not
two coincident reflected FaceIds. Its previous free triangle
arrangement could choose an asymmetric ear scaffold; the new
source fan builder uses its positive-X conforming side once and
mirrors every triangle in reversed winding, sharing the existing
on-plane source point indices and the ONE new fixed point index.
All resulting children must lie within the original oriented
source triangle, preserve total area and have complete incident
fans. `RegionEdit` extends the prior V16 one-edge
`splitSelfMirroredFace` operation to a full strict finite
`replaceSelfMirroredFace` child fan. It supports distinct
reflected child FaceId pairs and genuine self-mirrored singleton
child faces, but never manufactures an all-X=0 coincident pair.
Original source FaceIds keep one live rewritten descendant,
new child FaceIds have exact parent provenance, and all new
off-plane cut points use reciprocal mirror VertexId pairs.
Every genuinely new on-plane point uses
`appendSeamVertex` ONCE and is its OWN mirror map entry.
The local COW edit checks all affected edge incidence and vertex
fans before creating its unpublished immutable root.

The conformance regression now enforces full three-contact V13
coverage with an independent c2 certificate, every inserted
vertex accounted for by exactly one paired or singleton orbit,
every source-owned child accounted for by exactly one reflected
face-orbit, reverse winding of all child partners, exact old
corner coordinates and old reader/picking identity. Incomplete
original V13 source pairs, incomplete historic mirror maps and
exhausted mirrored orbit work never return a certified private
candidate. The two components are not Boolean-welded here:
the purpose of V17-M06 is exclusively ORIGINAL source CUT
topology with exact self-reflection, not material classification,
retirement, published history, or a complete Boolean union.

This separation was intentionally decided and planned BEFORE
further implementation in the flat milestone list; V17-M07
is the independent same-objective closure/atomic-publication
follow-on. An earlier exploratory X=0 one-Step attempt reached
c3 but found 'Conflicting child-contour vertex mirror mapping':
the historical mirrored c3 contour classifier still assumes
two distinct VertexIds and FaceIds for each reflection orbit,
and cannot yet authorize a self-paired fixed-point boundary.
The partial c3, d2, Step and worker changes from the exploratory
branch have been withheld from M06's accepted runtime source
path. M07 must fix the refusal with true original provenance
and valid invariant disk fans, never by weakening the V13/V15
cavity or unique-edge predicates; only then can it test a
single original-root final CAS, one undo/redo history entry,
the worker's three synchronized mirror controllers and a
durable exact mirror-orbit checkpoint. Both the old M05 off-plane
positive acceptance and V17-M08–M13 downstream goals remain
unchanged; the desktop Merge geometry option is still disabled.


## V17-M07 — Source-proven X=0 c3 reflected child and contour orbits

**October 18, 2026; scope boundary and implementation.** The positive
private M06 original cut revealed a failure of representation, not a
license to weld: c3's historical mirrored deletion code assumed that
every FaceId and VertexId reflection orbit has cardinality TWO.
Its `addPairedVertex` rejected `a == b`, its face orbit count was
`2*pairCount == childCount`, its inserted vertex count was similarly
restricted, and contour mate discovery searched only `j > i`.
An original source-proven fixed point at X=0 can have a single
stable VertexId and a genuine invariant triangular child can have
a single FaceId; an actual reflected contour can have one index
representing the whole closed ring.

**Forward planning made before changing the runtime gate.** This
proved too broad for a single c3-plus-d2-plus-CAS milestone.
`todo.md` and `docs/V17_MILESTONES.md` now divide the SAME
original full M07 acceptance into three independent flat peers:
M07 positive PRIVATE cavity-safe c3 mirror/contour provenance;
M08 exact-source d2 welded manifold PRIVATE union; M09 final
original-root Step/worker/undo/redo/checkpoint. The formerly planned
M08–M13 objectives move unchanged to M10–M15. No deep subtask
hierarchy or empty checkbox masquerading as a finished union.

**c3 source-authority representation.** Mirror child FaceId orbits
are stored canonically as (a,a) for a single genuinely invariant child
or (a,b) for two distinct reflected children. The verified involution
map contains both directions for a paired orbit and just one
entry for a singleton. Each FaceId in the FULL `childSources`
map is accounted for ONCE. Source originals must themselves
have exact reversed reflected face XYZ/winding, and each actual
child triangle must have a compatible reversed reflected vertex
cycle; invariant child triangles require precisely one source-fixed
corner and a mutually exchanged off-plane edge. New fixed IDs
are authorized by the M06 `mirroredSeamVertices` journal and must
be absent from the ORIGINAL root with exactly X=0. Historical
candidate fixed IDs must be actual corners of their genuine
ORIGINAL source faces, still present in the real child face, with
unchanged X=0 source coordinates; a coincident arbitrary new
point is NOT promoted to an old fixed mirror orbit. All inserted
vertex IDs must be accounted for by exactly one journal entry:
2 per ordinary pair, 1 per verified fixed point. Both count and
involution checks are enforced.

**Reflected removal and rim ownership.** c3's reliable c1
positive signed-winding decision and independently recomputed
complete c2 finite original-contact edge evidence remain mandatory.
Exposed and reverse cavity classes are not deleted. FaceId removal
journals use one (a,a) entry for a singleton, one (a,b) entry
for a genuine pair, and expand counts by 1 or 2 respectively.
V15's contour shape scoring remains unchanged; no synthetic
Ready permission is created. Each directed closed contour ring
must have a UNIQUE reflected partner (which can be itself).
Each self-reflected contour is checked for exactly two source
fixed boundary anchors under the reversed cyclic vertex
involution. A loop with an unmaterialized crossing-edge midpoint,
duplicated contour corner or competing reflected mate refuses.
Caps and zipper choices are verified for reflection closure as
before, including permissible fixed contour orbits. This is a
read-only provenance/nomination decision: it does NOT call
RegionEdit, remove a face or weld unrelated source sheets.

**Proof boundaries retained.** Invariant contour certification
does not alone prove that two separate original sheets may be
physically welded. Existing `joinCertifiedCutSections` still
rejects a self-reflected seam equivalence class and does not
yet accept a fully validated on-plane disk/link. The original
source-proven d2 exact coincident survivor-interval match,
final closed edge/vertex manifoldness, no residual collisions,
cavity safety, one original-root history transaction and queued
worker mirror journal are all separately required in M08/M09.
Production V13 predicates and V15 scoring thresholds are NEVER
weakened to force this fixture; as in prior M04/M05 acceptance,
the synthetic tetra regression alone may configure more permissive
TEST-only V15 shape metrics. Automatic sculpt-triggered Merge
geometry and the desktop switch remain disabled.

**Regression target.** Extend the EXACT M06 two-closed-tetra
complete three-original-contact fixture, retaining the original
root and independent c2 coverage. Require positive c3 Ready,
fully classified source children, authorized and protected
FaceIds, exact singleton/pair removal counts and unique
contour involutions with a real fixed rim having two source
fixed anchors. Broken singleton child journals, missing fixed
inserted identity, missing original V13 pair, exhausted child
work and the prior M06 genuine grazing fixture must refuse
with no approved partial deletion or old-root mutation. The
off-plane 24/32-contact M05 results remain regression controls.

**M07 verification follow-up, October 18:** The strict fixed-ring
regression (two self-orbit source anchors, at least one real invariant
contour), exact child classification/removal and preserved original
reader identity passed 23 CPU suites and all applicable build/benchmark
checks on Linux GCC, Linux Clang and Windows MSVC at code commit
9b4a0a6313e4e3b649059a61a35f9d935ec63c7b
(Actions run 39135929681). M07's private c3 certificate is accepted;
M08 PRIVATE d2 terminal X=0 disk weld and M09 original-root one-Step
worker/history checkpoint remain explicitly OPEN. No live toggle.


## V17-M08 — Source-proven X=0 private exact Boolean seam join

**October 18, 2026; accepted scope and design boundary.** M07
established positive c1/c2/c3 removal authority for the full
three-original-contact reflected two-tetra cut; it did not merge
the surfaces. The older d2 join had two distinct assumptions
that excluded legitimate invariant cases: (1) the combined original
plus new cut VertexId involution forbade every a==b mirror mapping,
and counted all insertions as two-ID pairs; and (2) the reflected
union representative selector rejected every class whose reflection
returned to its own source-proven seam equivalence root.

The d2 `mirrorMap` now retains genuine self IDs from the HISTORICAL
original involution and from `mirroredSeamVertices` (the M06
source-fixed new-ID journal). An inserted pair must still contain
two distinct off-plane IDs. Every recorded new ID must be accounted
for exactly once (one self ID or two paired IDs) and every original
entry must remain reciprocal with exactly reflected XYZ. Historic
fixed corners on X=0 do not become independent new points.

**Exact original-source weld decisions.** A candidate seam edge
must still be on an actual original V13 face-pair section, between
real c3-surviving children of those specific source FaceIds.
Both source-owned surviving intervals must have the SAME exact
3D endpoints and opposite orientation; no coordinate hash,
nearest-neighbor or approximate colocation can grant a weld.
A union-find class maps to itself under reflection ONLY when
EVERY contributing sheet-local stable VertexId maps to itself,
is EXACTLY on X=0, and has independently verified provenance:
either the newly inserted M06 fixed-cut journal or a historic
X=0 original source corner that actually survives on the
corresponding child and has its own self mirror map entry.
A class containing any genuinely paired ID is never folded into
one fixed vertex. Non-invariant classes are required to form two
reciprocal classes of equal cardinality; each retains its
different physical representative ID.

**Private retirement/manifold closure.** c3 deletion authority,
M03 optional complete unchanged-original authorization and
all protected/reversed cavity classifications are recomputed;
a user-supplied low-level contour or provisional cut does not
confer authority. The same single unpublished RegionEdit
removes authorized buried source children, substitutes only
source-paired seam IDs, retires actual orphan stable IDs and
validates its local links. The completed immutable private root
must have closed oriented opposite edge incidences, one connected
incident fan for every vertex and exactly one connected surface
component in the true three-contact two-tetra positive. An
independent test traverses *every* terminal FaceId, checks
oriented edge incidence and each vertex's connected disk link,
and performs a whole-final non-adjacent intersection audit,
not merely the newly edited boundary. All original protected
corner physical positions remain exact; the original revision,
picking root and readers remain unchanged.

**Uncut originals.** M03's reflected untouched-original survey
also had the earlier off-plane-only assumption, rejecting all
source-proven original fixed VertexIds and invariant FaceIds.
That independent survey now accepts original reciprocal or
self-reflected vertices only with exact X=0/XYZ proof. It
allows an invariant original FaceId only if the reverse reflected
cycle is identical to itself with precisely ONE fixed original
corner and two exchanged off-plane corners; duplicate physical
faces and invalid plane-only degenerate rings remain refused.
Its protected and authorized whole-original FaceId journals
count true singleton orbits as one and off-plane paired orbits
as two. The private join offers the independently classified
uncut M03 survey path in addition to child-only c3 removal.
This finishes original-source-side geometric acceptance that
would otherwise silently block M09's `includeAuthorizedUncutOriginals`
join path.

**Positive/negative regression and limits.** The exact M06
two-closed-tetra complete THREE original V13 pair fixture is
passed through a real private 2→1 cut/c3/d2 union. Test the
full current indexed mesh for ONE connected closed edge and
vertex manifold; complete nonadjacent contact absence;
physical mirror orbits for all surviving known IDs, genuine
self-fixed X=0 vertices, original XYZ fidelity, retired
FaceId/VertexId tombstones, and old-root pointer immutability.
Also request the independent M03 source survey and prove the
same private manifold union still closes. Missing original
contact, corrupt historical fixed mirror mapping, too-small
seam work and the previous M06 grazing fixture must produce
NO final revision, history journal, approved partial retirement
or guessed source ID. The production V13 and V15 predicates
are untouched; synthetic tetra tests use the existing
test-only quality limits. Off-plane M05 unions remain controls.

**Not M08:** This is an immutable, private final cut+join revision,
not a one-original-root composed Step. No worker CAS, history
record, checkpoint, renderer dirty update or desktop toggle is
enabled. M09 owns exact source-root net journals and all three
worker mirror maps, undo/redo and durable persistence; M10
further records the overall supported/refused geometry envelope.

**M08 verification follow-up, October 18:** The complete
M06 three-original-contact X=0 2→1 private source-paired union,
full final connected oriented edge/vertex-link and nonadjacent
collision audit, original reader/tombstone identity, damaged
historical X=0 mirror map/section/budget refusals, and the additional
M03 source-certified unchanged-original survey/join mode all passed
GCC, Clang and MSVC 23-suite CPU/benchmark CI at code head
fefa92665501dd007849a6fa80a4e0c7b7940361
(Actions run 39138641095). This establishes the independently
complete M08 private geometric acceptance only; M09 remains the
first acceptance for composing intermediate COW revisions into
a SINGLE ORIGINAL-root publication, with net fixed-point mirror
journal, three-controller worker, one undo/redo and durable checkpoint.
No automatic/live Merge geometry has been activated.


## V17-M09 — X=0 ORIGINAL-root Boolean Step and worker mirror history

**October 18, 2026; scope.** M06 established the complete
three-original-source-contact on-plane cut with one permanent
self-mirrored ID per independently proven fixed point. M07 added
genuine c1/c2/c3 removal, protected cavity and invariant contour
proof, and M08 proved one privately staged 2→1 exact Boolean union
with connected oriented vertex links and independent M03 uncut
source checks. All those intermediate roots stay PRIVATE: neither
an original-root CAS nor worker mirror-map updates may expose
them individually.

**Net original-root journal.** `prepareExactCutStep` continues to
stage one source cut and one certified d2 terminal join and to
compute only the NET original-to-final delta, excluding every
VertexId/FaceId that was allocated and retired in an invisible
middle COW stage. Its original historical vertex retirement proof
now distinguishes true self orbits (a,a) at original X=0 from
paired off-plane (a,b), requiring exact original geometry and
complete retirement of the entire original involution orbit.
A surviving M06 new `mirroredSeamVertices` source-fixed ID
becomes ONE new (id,id) addedMirrors history entry only if it is
genuinely absent from the original root, lives at exactly X=0 in
both cut and final roots and is present in the NET addedVertices.
A born-then-retired new self ID contributes NEITHER to the live
new mirror journal nor the original-root retirement journal.
Every off-plane pair still remains distinct and either both
survive with exact reverse coordinates or both retire. Their
aggregate inserted-ID cardinality must equal two times paired
orbit count plus one times singleton count, with no duplicate
permanent IDs. Surviving original or inserted child FaceIds
must retain the verified reflection winding, permitting a
genuine invariant (face,face) stable FaceId singleton. Dirty
partitions derive only from the private source/join write
owners whose ORIGINAL and FINAL persistent pointers differ;
the one composed Step also accounts for all COW allocation
costs, net count deltas, full changed-halo collisions and
protected exposed/reversed cavity source triangles. Work caps
refuse a complete publishable Step without exposing private
cut/join roots or partial history journals.

**Three-controller worker transaction.** The explicit async
`mergeExactContacts(expected,completeOriginalPairs,limits)`
command binds the original expected pointer at enqueue time.
Preparation never reads an in-between root as the worker's
new expected root. Before CAS, it verifies Inflate/Refiner/
Collapser map agreement and stages true historical self and
off-plane retirement plus new fixed/off-plane additions in
the Refiner and Collapser speculative mirror journals.
One (id,id) journal entry inserts or removes one map key,
not two, while a true off-plane pair inserts/removes two.
The target final index is checked for every new live orbit's
EXACT reflection and historical old IDs are forbidden in
addedMirrors. An invalid or stale map, missing V13 source
contact, net work exhaustion, validator refusal or a stale
original CAS refuses without changed root/history or
published renderer dirties. Only after the ONE successful
original CAS does the worker apply the identical deltas to
Inflate, append ONE undo/redo history transition and expose
the final root/dirty partitions to renderer and picker.
The already documented unexpected post-CAS fatal failure
boundary remains M14, not silently called recoverable.

**History and durable identity.** Undo restores the exact held
original immutable root and its complete original X=0
singleton/paired mirror involution. Redo restores exactly the
terminal X=0 union and only the surviving singleton/paired
new journal entries. The checkpoint format already stores a
mirror entry as two stable IDs; (id,id) is valid and occupies
one record, with exact live-ID validation. Save/reload preserves
all surviving mirror pairs and fixed-point records together
with retired FaceId/VertexId tombstones, stable identity
watermarks and the terminal (never intermediate) mesh.
A newly restored symmetric async worker must recreate all
three synchronized mirror controller maps from that same
durable set and conservatively refuse stale-source contacts.

**Acceptance/fail-closed limits.** The positive fixture is the
EXACT M06 two-tetra source (THREE strictly transverse original
V13 pairs), not a synthetic subset of contacts. It must
prove one connected collision-free terminal union, final
incident link topology, both real fixed and off-plane net
mirror orbits, retired orphan IDs, exact source original
coordinates, ONE CAS with stale replay refusal, one
undo/redo history transition, queued worker failure followed
by success and future input draining, and durable recovery.
Missing one source pair, malformed original off-plane mapping
forged as a self-pair, stale expected root and work cap refusal
must leave the original index and all mirror journals untouched.
M11 still owns automatic physical sculpt-triggered contact
unions, M14 owns unexpected post-CAS failure atomicity and
M15 owns default-off desktop activation. There is no general
claim for arbitrary tangential, coplanar or high-degree
junction topology.

**M09 verification follow-up, October 18:** The true complete three-
original-V13-contact reflected two-tetra Step passed the one-original-
root CAS, exact net fixed/paired mirror/tombstone and dirty owner
journals, private 2→1 closed vertex-manifold, full collision
checks and protected original geometry. The queued async worker
demonstrated rejected incomplete original contact followed by
successful X=0 final Step, stale original root rejection, ONE
history undo/redo with restored singleton mirror maps and durable
save/reload into a freshly constructed symmetric worker.
One earlier regression compared original pairs in declaration
order against canonical checkpoint pairs and was corrected in
the TEST, without changing any runtime history semantics.
Complete GCC/Clang/MSVC warnings-as-errors compilation, 23
CPU suites, remesh benchmarks and Windows Quickhull at code
commit 29fec5607d9fcc6da70a2c0722076a4197de79e2
(Actions 39139919037) were successful. The code and docs now
mark the independently tested original-root M09 gate accepted;
M10–M15 and overall live V17 acceptance remain separate.

## October 18: source-proven coincidence is a general 3D topology rule (planning decision)

Coincident coordinates do NOT imply a shared VertexId or authorize a weld,
whether X=0 or not. X=0 is the fixed-point set of Mesher's CURRENT
reflection R(x,y,z)=(-x,y,z); it adds exact self-orbit and invariant
face-fan constraints, but is not the source of the underlying
coincident-vertex problem. Distinct valid components may touch at
exactly one XYZ; blindly replacing both IDs would create a disconnected
one-ring. On the other hand, separately source-owned conforming
section points with identical XYZ MUST become one representative after
a complete original-V13-paired, c1/c2/c3/M03-authorized, oriented,
closed/manifold-valid join. Near-equality alone never qualifies.

The precise invariant, decision sequence, positive/refusal fixture
matrix, and scope boundary are now recorded in [COINCIDENT_VERTEX_TOPOLOGY.md](docs/COINCIDENT_VERTEX_TOPOLOGY.md).
A new independent peer V17-M16
is scheduled after M10 and before M11 and M15; previous milestone
IDs, historical accepted work, and the broader V19 stress program
are unchanged. This document records a PLAN, not evidence that
arbitrary coincident imported geometry is supported.


## V17-M10 — Complete original-contact supported/refused domain

**October 18, 2026; explicit distinction between *safe refusal* and
*positive support*.** An old V17 proof gap was to demonstrate that
a local triangular arrangement of TWO selected noncoplanar sections
was conforming, and then treat that as evidence a closed THREE-shell
Boolean input could work. The historical three-tetra crossed-sections
and interior T-junction meshes each expose **21** genuine original
V13 source pairs. Their selected two curves cut correctly, but
the COMPLETE twenty-one includes nineteen additional nontransverse/
grazing original face pairs. V13 source conformance MUST refuse
rather than skipping those nineteen or fabricating a final original-
root Step. Added a deterministic full-21-source test for BOTH historical
meshes and REVERSED input order, requiring no candidate expected/
final/commit, no net VertexId/FaceId/mirror/history/dirty journal,
unchanged source pointer identity, no tombstones, and all THREE old
connected closed surfaces unchanged.

**Other explicit source-driven boundaries.** An isolated exact
vertex touch between two tetrahedra does NOT manufacture a finite
two-sheet transverse section or permit identity merging. Two
independent fully coincident, coplanar tetrahedral shells do not
have unique source-oriented transverse seam intervals and must
refuse. A completely nested tetra without surface intersections
cannot use c3/M03 to silently retire an unrelated closed shell:
the original V13 contact set is empty and an explicit Step must
refuse. These are separate cases from M16's wider coincident-
vertex identity acceptance, whose documentation is now merged
as PR #61; M16 stays an independently COMPLETABLE peer after
M10 and before M11, never a change to the old M01–M15 IDs.

**V16 adversarial lineage.** The exact original-root Stage reuses
the actual V16 two-cube volumetric, 1e-7 near-coplanar and exact
plane-tangent examples, plus a rotated and affine-sheared three-
cube multi-shell group. The full V13 query, never a selected
face subset, drives `prepareExactCutStep`. For such numerically
awkward fixtures, no accepted result is legitimate unless the
terminal root demonstrably contains exactly ONE connected,
closed oriented and vertex-link-manifold union with NO residual
nonadjacent collisions. Otherwise, the only allowed result is
a complete refusal with stage reason and NO partial net Step
or mutation of the original picker/renderer root. This bounded
validation does not claim every rotated or near-coplanar Boolean
configuration is positive; broader imported pathological cases
remain V19.

**Classification matrix and work checks.** The authoritative
case-by-case supported, refused and unsupported contract is
[docs/V17_M10_SUPPORTED_DOMAIN.md](docs/V17_M10_SUPPORTED_DOMAIN.md).
Its positive controls remain unchanged complete M04 generic
TWELVE and orthogonal SIXTEEN section three-tetra unions,
M05 off-plane reflected TWENTY-FOUR/THIRTY-TWO section unions,
M08/M09 complete THREE-contact X=0 fixed-point two-tetra union,
the M03 wholly buried original base with protected reversed
cavity, and M02 genuinely unequal finite seam density reconciled
by source evidence. Those controls are actual connected/manifold
positive terminal unions, NOT examples of successful refusal.
Independent hard caps on original-root net stable-ID work,
source-section d2 trace predicates and the FINAL changed-halo
collision audit now explicitly refuse otherwise positive geometry
when complete verification is impossible; the original reader,
history, net mirror and retirement journals remain unavailable.
Original corner coordinates, c1 positive signed-winding occupancy,
protected cavity/outer faces and existing V15 production quality
thresholds remain mandatory.

**Workflow gate.** These are bounded test fixtures and honest
supported-domain diagnostics, not automatic physical input
triggering. M16's source-proven equivalence gate is next after
M10, before M11's physical alternating-tower sequence; M12–M15,
V18 and V19 keep their established objectives.


## V17-M16 — Provenance before geometric coincidence in all coordinate regions

**October 18, 2026; independent gate after M10, before M11.**
Previously, the invariant-reflection fixture at X=0 dominated the
development discussions because fixed-point mirror orbits require a
single stable (id,id) vertex and an invariant child FaceId fan. That
is a separate algebraic reflection constraint, NOT a reason
unrelated vertices sharing XYZ anywhere should merge. More
generally, a shared world coordinate is compatible with several
topologies: two independent surfaces and their separate stable IDs;
two independently materialized cut endpoints later joined with
complete original V13 interval proof; a point-only or tangential
contact for which no source-paired finite seam exists; or an
incompatible three-/four-sheet branch which cannot be made manifold
by naive global deduplication.

M16 promotes the previously documented
[coincident-vertex identity contract](docs/COINCIDENT_VERTEX_TOPOLOGY.md)
into source-to-terminal regressions with an explicit
[acceptance register](docs/V17_M16_COINCIDENT_IDENTITY_ACCEPTANCE.md).
An accepted off-X=0 two-tetra THREE-contact source union must
prove all original contacts, independently exact matched but
DISTINCT VertexIds in the conforming *private* cut, complete
c1 positive signed winding, c2 finite section trace, c3
cavity-safe retirement, and ONLY then d2 exact original-contact
survivor interval joining. A truly connected closed oriented
and vertex-link manifold 2→1 terminal result (also one composed
ORIGINAL-root Step) proves that matching source endpoints do
eventually become one terminal permanent VertexId. Reversing
the full source-pair order is an acceptance control, not a
selected subset of contacts. No source coordinate moves
and no global XYZ map is consulted to choose representatives.

The reflected positive control is separate: the complete
off-plane reflected V13 fixture starts with opposite sheets'
own unique stable endpoint IDs despite exactly matching XYZ
on each intersecting pair; the final two mirrored joined
components have no duplicates on a certified terminal seam
and each has proper vertex links. Reflective off-plane
equivalence classes remain RECIPROCAL (two distinct IDs).
The established on-plane M08/M09 fixture can have a TRUE
self-paired ID (one ID) ONLY with original fixed-point source
provenance; an unrelated coincident on-plane corner must
remain a different ID.

The negative controls prove this is a topological,
coordinate-independent safety rule. Two valid tetrahedra
touching only at ONE vertex, off-plane or exactly X=0,
retain BOTH stable source IDs and TWO individually closed
connected one-rings; their local no-op does not deduplicate
world coordinates, and an explicit Boolean union request
must refuse without a finite V13 source interval. Four
mutually tangent tetrahedral cones having four distinct
permanent IDs at EXACT (0,0,0) cannot be welded into a
single branched four-fan vertex. Two source points separated
by only 1e-7 remain nonidentical and cannot be snapped or
inferred as a valid source curve. Tampered source-owning
FaceIds, omitted whole original contacts, incompatible
mirror maps, untraced/over-budget seams and uncertain
tangent/coplanar or cavity classification must refuse with
no final root, retirement or mirror/history delta. The
existing M02/M03/M04/M05/M08/M09/M10 regressions remain
real positive or fail-closed gates rather than being
reinterpreted as evidence for universal imported topology.

**Limit of acceptance.** M16 is NOT a global redundant-vertex
cleanup operation, arbitrary plane reflection generalization,
or generic N-way Boolean algorithm. Unsupported independent
coincidences can remain in an immutable source revision
without invalidating their separately connected vertex
fans. Pathological imported topology, arbitrary tunnels
and high-degree four-sheet welds remain in V19's stress
scope; M11 is still the independent physical sculpt repeat
gate, M14 covers post-CAS fault atomicity, and M15 alone
enables the opt-in desktop Merge geometry switch. V18's
Off-mode collision controls remain separate.

**M16 bounded verification follow-up (October 18, 2026):** The
complete off-X=0 three-original-contact source proof, distinct
prejoin exact-coincident per-sheet stable IDs, certified 2→1
private and ORIGINAL-root terminal joins, reversed source order
and true reflected off-plane seam counterparts passed. Two
independent touching closed shell fans both on/off-plane,
four unrelated tetrahedral one-ring fans at EXACT (0,0,0),
a 1e-7 near miss and a third incidental touching shell next
to the otherwise-active source join preserve separate IDs
or refuse without inventing authorizing finite intervals.
Incomplete source FaceId coverage, forged original child
ownership and exhausted exact seam budgets return no final
root/history or dirty/mirror journal. M02/M03/M04/M05/M08/
M09/M10 regression controls remain intact.
Windows MSVC /W4 /WX, GCC, Clang, all 23 CPU suites and the
applicable benchmarks passed in code-head PR Actions
39144616634 on commit 4979b25. Early MSVC C4456 errors
were TEST-only nested local variable shadowing and were
corrected without relaxing original-contact or manifold
checks. The bounded accepted cases are cataloged in
docs/V17_M16_COINCIDENT_IDENTITY_ACCEPTANCE.md. No global
coordinate deduplication, auto Merge geometry switch or
unsupported higher-sheet Boolean welding is enabled.
M11 remains NEXT; M12–M15/V18/V19 are unchanged.


## V17-M17 — Complete source contact certification on a PHYSICAL private sculpt candidate

**October 18, 2026; independent post-M16 gate.** The V16
`evaluateSculptCollisionCandidate` approximates contour scar
excision, while the M01–M10/M16 *exact* Boolean path consumes a
COMPLETE ORIGINAL V13 FaceId contact set. Those paths must not
be conflated. A physical stamp's initial root, intermediate
post-Inflate root and post-refine/collapse candidate have
different persistent revision identities and potentially
different FaceIds; a selected changed-face pair cannot
certify that all neighboring original contacts were inspected.
M17 introduces `assessPostSculptOriginalContacts` as a
read-only bounded query on the ACTUAL supplied candidate root.
It neither mutates nor publishes the candidate.

The evaluator scans at most a declared 16384 live candidate
vertices and 32768 faces, using the persistent indexed BVH
and bounded per-query box, partition, narrow-phase and
reported-pair work. Every canonical FaceId collision is
independently checked for one strict finite original-source
V13 transverse segment. The *entire* pair set belongs to
the exact returned candidate root: `CompleteTransverse`
has no unsupported member; `Clean` means no reported
V13 contact, not a license to infer nested or tangent
Boolean geometry; `Refused` carries a diagnostic but
NO usable candidate pointer or partial pair list. Candidate
work exhaustion, an unsupported point/coplanar/grazing
source pair, or missing/reflected nonreciprocal mirror map
refuse at source classification.

The acceptance fixture uses real `VersionedInflateBrush`
deformation on a known supported three-contact off-plane
two-tetra geometry and its independently mirrored twin;
the resulting three/six actual V13 FaceId graphs match
a separate all-live original-source query. A second test
begins with strictly disjoint closed tetrahedra and uses
true simultaneous normal-driven Inflate to CREATE a
transverse intersection; the pre-stamp state returns
`Clean`, whereas the first certified post-stamp
candidate returns a complete finite source graph.
A subsequent genuine local edge split probes the
rewritten source FaceIds, accepting a complete rechecked
finite graph or refusing an unsupported remesh candidate
without leaving stale pairs. Strict one-below limits,
invalid mirror orbits, point-only contact and coincident
coplanar shells are fail-closed controls; held original
picking/BVH roots, permanent VertexIds/FaceIds and
history/renderer state remain unchanged.

**Boundary to M18:** This source certificate is only
the FIRST prerequisite of a future one-original-root
physical Boolean Step. M18 must build a private exact
c1/c2/c3/M03/d2 union against the candidate root,
then compose the candidate's Inflate/refine/collapse
journal and final Boolean into one captured original
CAS, mirror/controller history and renderer update.
M11 then proves repeated sixteen alternating real
symmetric physical unions with queue/refusal/clean
continuation. No desktop Merge geometry toggle or
new runtime welding shortcut is enabled in M17.
The bounded behavior and limitations are recorded in
docs/V17_M17_PHYSICAL_CONTACT_DISCOVERY.md.


## V17-M18 — One atomic physical sculpt source-certified Boolean Step

**October 18, 2026; independent M18 boundary.** The already proven
M17 source-contact evaluator is now consumed INSIDE a privately
prepared actual Inflate/refine/collapse brush Step, rather than
only as a read-only offline query or the separate explicit
`mergeExactContacts` worker command. The specific opt-in flag
`SculptCollisionSettings.physicalExact` defaults to FALSE;
it is mutually exclusive with V16 `collision.enabled`, whose
approximate contour repair is NOT a substitute for an exact V17
Boolean. The live desktop UI still constructs the unchanged
default policy and does not expose or enable Merge geometry.

A physical Step begins with the immutable published ORIGINAL
index, applies the actual normal-driven stamp and enabled
local splitting/flipping/collapse, and classifies the complete
post-remesh CANDIDATE source root with M17's finite V13
original FaceId certificate. Clean input continues as an
ordinary physical stamp; every unsupported, ambiguous,
nontransverse, mirrored-source-broken or over-budget query
returns a refused entire Step, without committing the
remeshing candidate or partial changes. For fully certified
transverse candidates, the original-contact graph and its
own exact candidate-root pointer are passed unchanged to
`prepareExactCutStep`. That API privately implements
c1/c2/c3/M03 occupancy, cavity-safe retirement, source
subdivision and d2 finite interval welding, independently
proves the final closed oriented manifold, vertex links
and residual collision halo, and returns a final immutable
root. NONE of the initial Inflate, local remesh, cut or
preclosure revisions are published separately.

The original-root transaction is the BRUSH'S captured
pre-stamp revision, not the Boolean planner's private
post-remesh candidate `expected`. The exact join's
candidate-to-terminal added, rewritten and retired source
IDs extend the pre-union physical dirty set; renderer owner
changes are computed once between initial and terminal
immutable revisions. The speculative Refiner/Collapser
mirror journals incorporate new refined IDs, collapsed
historical IDs, the exact source-orbit retirements and
real cut-generated additions before committing. Comparing
the historical Inflate map to final live speculative
orbits produces the ONE NET original-root history: every
unchanged old VertexId retains the identical original
mirror partner, all final IDs form complete exact reflected
reciprocal or X=0 self orbits, and newly allocated IDs
that are themselves retired before final publication
never appear in undo/checkpoint as live additions.
No global coordinate-equivalence welding or full mirror
map copy is used to select representative IDs.

The existing stamp's single `publishIfCurrent(old,final)`
publishes only the complete immutable topology after all
M17, V17 Boolean, source-pair, mirror, dirty owner and
caller validator gates pass. Inflate gets its net mirror
deltas AFTER this one CAS; ONE history entry records net
mirrors, copied payload bytes and actual terminal vertex/
face counts. The same root and owner journal feeds the
asynchronous worker's unchanged FIFO 4D stroke and
renderer mailbox. Refusal before CAS rolls back both
speculative remesh controller maps, leaving original
picker/BVH, Inflate and history/dirty checkpoint unchanged,
so the next physical stamp remains usable. Exceptions
after a successful CAS are separately M14's explicit
fault-atomicity program, NEVER a recoverable refusal.

The small bounded acceptance fixtures use an M16
off-X=0 complete three-original-contact two-tetra
transverse source with real `VersionedInflateBrush`
movement; its exact c1/c2/c3/d2 private source union
physically welds to a closed collision-free 2→1 mesh
through one original-root Step, with undo/redo and a
durable immutable checkpoint. A separate true
**pre-contact clean** pair of disjoint tetrahedra is
physically inflated through the finite source-contact
boundary; only a real M17-positive and true exact final
weld counts as success, not a declined unsupported
stamp. A real post-Inflate local edge split alters
the candidate's original FaceIds; individually bounded
alternate moved edges demonstrate a genuinely accepted
source-certified split-plus-union transaction while
every unsupported split must roll back atomically.
The reflected two-half physical case checks net
reciprocal mirror maps and durable historical restore,
including a deliberately refused terminal validator
Step followed by a valid physical input on the SAME
brush. The async 4D worker test checks a genuine
input-triggered original-root CAS/dirty snapshot, one
history undo/redo and durable checkpoint, without
using the explicit merge command.

These acceptance domains do NOT imply all arbitrary
post-refine/collapse arrangements, tangent/coplanar
surfaces or 3+/4-way junctions become supported.
Source-proven coincident IDs continue to follow M16.
M12 owns anchored cumulative exposed witnesses and
contact hysteresis, M13 aggregated work telemetry,
M14 fault-injected post-CAS atomicity, M15 the opt-in
desktop switch. **Original M11** remains the independent
sixteen real alternating symmetric physical unions
and FIFO cleanup acceptance after this first physical
positive. The precise tests and limitations are
catalogued in docs/V17_M18_PHYSICAL_EXACT_STEP.md.


## V17-M11 — Positive repeated physical source-exact unions in two reflected towers

**October 18, 2026; independent post-M18 repeat/FIFO boundary.**
M18's first physical exact Boolean is not sufficient evidence
for sixteen repeated 4D worker unions. The earlier V16 repeated
two-tower test could count refusal as a safe outcome. The M11
source fixture uses SIXTEEN different initially CLEAN closed
two-tetra contact regions instead, eight each on two spatially
separated towers, with independently exactly reflected negative-X
sheets and complete reciprocal VertexIds. The physical input
alternates tower A/B and advances height bands; radius and
separation ensure only one independent pair on each reflected
half is moved per stamp. At all sixteen inputs, genuinely
normal-driven Inflate must CREATE finite original V13 source
contacts. The current M17 all-candidate discovery and M18
private source-proven c1/c2/c3/M03/d2 union accept a real
closed and oriented two-half joined result; a refusal does
NOT satisfy the positive requirement. The next input begins
from that SAME published root, never from a fresh copy.
Accepted roots undergo independent full live V13 collision
queries, protected live mirror ID and 3D-reflection checks,
genuine FaceId retirement, nonempty dirty partition
publication and exactly one original-root CAS/history.
All old BVH/picker roots stay reachable and immutable.
The sixteen history entries undo back to the exact
initial pointer and redo through every previously held
terminal pointer.

The async original 4D FIFO worker independently delivers
sixteen physical begin/end commands, NOT explicit Merge
requests, with a complete final-only renderer snapshot for
each joined region. A distant clean command may produce
an unchanged snapshot but never an invented publication.
A terminal-validator refusal after speculative exact
staging also yields only an unchanged snapshot with no
history/mirror delta; the next identical physical command
succeeds on that SAME worker. A separate very broad
normal-driven symmetric physical stroke moves many
eligible source regions at once and exceeds a finite
M17 original source-work cap, refusing the WHOLE Step.
A subsequent normal single-region command on the same
work-bounded worker must then pass. Final queued durable
checkpoint, live-only reciprocal mirror orbits, and
sixteen independent queued undo/redo commands verify
history and checkpoint identity. Existing M10/M16/M17
negative geometry cases remain fail-closed and unmodified.

The separately accepted V16 thirty-two same-patch cap
replacement witness regression and earlier old-root
tests are retained; neither is rebranded as a supported
32-Step Boolean union. The M18 real post-Inflate
split-plus-exact-union and existing remeshing tests
also remain positive independent controls. M11 does
NOT prove endless remerging of one already welded
surface, arbitrary N-sheet imported geometry, all
local split/flip/collapse choices in the same exact
stamp, M12 original anchored cumulative behavior,
M13 global budget accounting, M14 post-CAS failure
atomicity, or M15 desktop opt-in Vulkan acceptance.
The live desktop switch still defaults OFF. The test
recipe and limits are in
docs/V17_M11_REPEATED_PHYSICAL_UNIONS.md.

## October 18, 2026 — V17-M12 flat provenance rebaseline, M19 fixed-source anchor

The accepted M11 sixteen physical exact unions are unchanged and the
desktop Merge geometry remains default-off. Examination of the old
SculptAnchoredSurfaceGuard and three-clean episode tracker shows
that static candidate FaceIds cannot prove lifetime preservation
after split/flip/collapse/d2 joins. Seven samples nearest to arbitrary
candidate triangles can falsely accept a different coincident source
sheet, while a previous-revision baseline can mask cumulative drift.
Intention cannot be inferred from the final distance or from reflection
symmetry alone. M12 is too broad to accept as one implementation.
Before source changes, the entire flat execution M19, M20, M21, M22,
M23 and unchanged M12 integration was recorded in todo.md and the
milestone document, retaining all original M12/M13–M15 objectives.

M19 introduces a narrow but sound read-only original-source contract:
the immutable first root is held by shared ownership, and a nominated
FaceId retains its ORIGINAL three ordered VertexIds, seven original
witnesses, and the minimum ORIGINAL edge length. An unchanged
FaceId with rewritten vertices is NOT the same proven patch;
neither are arbitrary triangles at coincident XYZ. Candidate comparison
requires exact original identity and incidence, failing closed for
retired/remeshed sources until M20's lineage. Exact finite cost is
seven source-local point-to-triangle tests per nominated source face,
with no partial audit at cap exhaustion. Cross-face nearest-neighbor
substitution is prohibited. The samples still constitute only finite,
one-sided distance evidence, NOT c1/c2/c3 positive winding,
cavity/deletion permission, complete collision proof, or a continuous
two-sided Hausdorff estimate. The tests use real Inflate motion,
unchanged original root/picker, spoofed face ownership and one-under
budget refusals. M19 never publishes geometry, turns on physicalExact,
or claims the later active/hysteresis policy is complete.

## October 18, 2026 — M20 flat proof work and M24 source-owned split certification

The M19 read-only seven-point original source-face guard cannot accept
a correctly remeshed original face once its FaceId/ordered incidence
changes. Source ownership and geometric proximity are different:
already-successful physical exact Boolean cuts have c1/c2/c3/d2
original FaceId provenance, while local V6 splits, edge flips and V7
collapse possess different and independently reviewable mutations.
The entire M20 execution M24 → M25 → M26 → M27 → original M20
integration was recorded in todo.md and V17_MILESTONES.md **before
M24 code**. Original M12 and M20/M21–M23/M13–M15 remain intact.

M24 records a committed split's exact immutable before/after root,
rewritten original FaceId and appended child FaceId for every true
original incident triangle. The independently read-only verifier
checks the actual directed oriented two-child subdivision, exact
midpoint and fixed-plane normalization, full split face/vertex
journals and simultaneous mirror orbits; one interrupted/forged
journal cannot authorize any protected patch. A first-original
protected FaceId maps to the retained rewritten same ID PLUS each
appended child through multiple real split generations and strict
pre-stamp immutable root identity. Identical XYZ never transfers
ownership to an unrelated triangle. Explicit work limits refuse
rather than returning an incomplete lineage. Edge flips, collapsed
original faces and c3/d2 Boolean children require M25/M26 before
M20 can be complete. This cannot turn on physicalExact policy or
desktop Merge geometry and asserts no unsupported fidelity guarantee.

## October 18, 2026 — V17-M25 protected flip/collapse source-transition proof

M24 established permanent-ID source ownership through true local split
commits, but intentionally refused flip/collapse. M25 extends the
same *read-only* source-lineage acceptance for actual committed
2→2 diagonal flips and collapsed edges without modifying sculpt
publication, checkpoint or UI paths. Runtime journals bind one
immutable pre/post root per accepted RegionEdit, its exact affected
permanent FaceIds/VertexIds and, for reflection, explicit source
vertex involutions. The independent verifier first checks M24's
actual split chain, then checks every flip's oriented four-edge
boundary and changed diagonal using permanent IDs, not XYZ nearest
neighbours. It requires full mirror-reflected oriented face fans
where symmetry was requested.

A protected flip requires both faces to descend from ONE certified
original protected FaceId, with conserved source-patch triangle area.
A flip crossing distinct protected source owners cannot reassign
either source's original material merely because the geometry is
valid; it refuses without a separately proven joint-source partition
certificate. This is a **conservative supported-domain bound**, not
a claim that no lawful cross-original flip exists. Accepted
unprotected physical flips preserve distant fixed-reference
features. Collapse is stricter until M21 deliberate-intent evidence
exists: its complete changed original source halo, midpoint/seam
geometry, stable tombstones and reflected endpoints are certified
from the committed roots, but any untouched-original protected
exterior/reversed-cavity patch involved in the collapse or moved
by its survivor refuses. A supported unprotected local/mirrored
collapse positively advances unchanged protected features and
retains prior reader roots. Finite evidence/living-face budgets
refuse atomically without converting uncertainty into permission.

M26 still owns V13/c1/c2/c3/d2 exact-child protected survivor
correspondence, M27 owns across-accepted-Step source patch/fidelity
composition and original M20/M12 remain open. M25 is no physicalExact
or Vulkan product activation, general area-sharing solver, or
continuous two-sided Hausdorff guarantee.

## October 18, 2026 — V17-M26 original source-certified Boolean descendants

Successful exact-cut original-root Step construction had already
validated c1/c2/c3, M03, private d2 seam joins, final residual
intersections, protected child fidelity, original stable mirror IDs
and net-history journals, but had discarded the child's original
FaceId provenance on returning the final candidate. M26 records
**private source evidence only on COMPLETE Steps**: the conforming
cut's original parent→child stable FaceId ownership graph, exact typed
child occupancy, protected/Buried/Outside selections, separately
certified M03 uncut original retention/retirement and source-owned
d2 seam replacements. These are immutable evidence for a read-only
consumer, never publishable private roots or history/checkpoint data.

Independent audit recomputes full original-source c1/c2/c3/M03
classification with the Step's same finite query budgets. Every
protected Exposed or ReversedBoundary descendant must remain live
at its same exact triangle XYZ even when d2 replaces seam vertex
identities; an original source may become empty ONLY via complete
certified Buried/Outside child retirement or exact M03 buried uncut
authority. Off-plane and accidental independent coincident sheets
remain distinguishable by source FaceId and stable vertex fan; X=0
singleton orbits must derive from the source cut rather than nearest
coordinate equivalence. Reflected final child face journals and d2
welded vertex roots are checked without copying global mirror maps.
The first original source root and all intermediate roots remain
COW reader-stable, with bounded evidence work. M27 will measure
fixed-original lifetime feature fidelity on these descendants; M21
owns deliberate intent and M22 activates live terminal acceptance.

## October 18, 2026 — M27 fixed-original source patch lifetime and witness accounting

M24, M25 and M26 independently proved source ownership across real
committed local splits, flips/protected-safe collapses and c1/c2/c3/
M03/d2 exact union children. The missing M27 lifetime contract was
**not** another approximate closest-triangle matcher. A first-nominated
FaceId retains its ORIGINAL first-root seven XYZ witness points,
per-original minimum edge scale and permanent source identity
through every accepted COW Step. A source with no surviving faces
is legal only with already certified c3/M03 Buried/Outside retirement,
never because the desired original protected geometry is absent.

M27's private read-only lifetime object keeps an opaque immutable
certificate (held by shared const ownership) of first and terminal
root pointer identities, first source FaceId → living descendant
FaceIds, wholly retired original sources, cumulative stage
evidence work and accepted transitions. This prevents a caller
from modifying a lineage or resetting the cumulative work cap
to slip an otherwise forged or coincident foreign source into the
protected set. Each advance reuses M24/M25/M26 validators rather
than trusting an invented union, and old readers remain COW-stable.

The seven finite first-original witnesses of a source are compared
ONLY with that source's certified descendants; each is normalized
by that original source's minimum edge, never brush radius or a
later refined edge. Preflight requires exactly seven distance
tests times the complete living descendant count, with equality
accepting and one-under refusing before partial sampling. A
separate **shared cumulative source-proof cap across all Steps**
prevents previous evidence from being discarded. Fully
Buried/Outside retirees are reported explicitly, never counted as
zero-deviation preserved original surfaces. This bounded, one-sided
diagnostic is not continuous two-sided Hausdorff, not intended
sculpt-scope permission and not active pre-CAS authorization.

Acceptance on GCC/Clang/MSVC in PR #71 includes two actual
published COW split Steps showing cumulative original-reference
drift, real safe flip/collapse, true source-exact unions, M03
burial and reversed cavities, reflected and X=0 fixed-point
orbits, and two independent exactly coincident closed shells
whose source ownership never aliases. Forged original anchor,
stale root, switched child owners, manipulated work/history
counters, missing retirement authority and all one-under
stage/distance budgets refuse. The original M20 final integration
remains separate; existing M21–M23 and original M12 scopes remain
unchanged, with desktop Merge geometry default-off.

## October 18, 2026 — V17-M20 original source-provenance integration acceptance

The full flat peer sequence M24–M27 is complete, and the unchanged
**original M20 source-certified remesh patch descendants** milestone
is accepted with PR #72. Detailed original-objective coverage,
positive/refusal matrix and remaining supported-domain boundaries
are preserved in `docs/V17_M20_INTEGRATED_SOURCE_ACCEPTANCE.md`.

The previous separately green stage tests established committed
source-oriented local splits, strictly same-original or unprotected
2→2 flips, full original source collapse retirement/tombstone proofs
and conservative protected-deformation refusal, exact source-owned
c1/c2/c3/M03/d2 surviving children and legitimate wholly buried
originals, reflected/X=0 fixed orbits and unrelated identical-XYZ
sheets. M27 sealed the original-root permanent FaceIds, per-original
seven witnesses/edge scales, living child graph, signed-winding
retirement authority and accumulated source-evidence cost.

M20's new combined test goes beyond these disjoint positives:
one actual connected accepted original-root exact union is followed
by a genuine committed local edge split **on its terminal root**.
ONE lifetime still references the same first-original FaceIds before
either Step; it preserves previously certified buried original
retirements while assigning new split children only via committed
source IDs. Both publish as distinct atomic successful repository
CAS revisions, while a stale CAS, forged source assignment, missing
evidence and one-under cumulative face/distance work cap refuse.
Original picking readers remain COW stable. The substantive M20
CI run #39168754914 is green on GCC/Clang/MSVC, all 23 CPU suites
and applicable benchmarks.

This is the completion of original M20 *source provenance*, not
an approval of deliberate protected surface destruction or an active
quality guard. M21 still owns explicit intended sculpt displacement
and cavity policy, M22 full final pre-CAS cumulative fidelity
acceptance/atomic rollback, M23 episode semantics, and M12 the
remaining integrated fidelity acceptance. M13–M16, V18/V19, the
source-local finite one-sided nature of the seven-witness test,
and desktop default-disabled Merge geometry remain unchanged.

## October 18, 2026 — M21 explicit original-source intentional feature scope

The original M20 source-provenance integration is already accepted,
but *source ownership* alone did not distinguish requested sculpt
deformation from accidental destruction of nearby exposed or reversed
cavity surfaces. M21 now requires the caller to nominate the intended
**FIRST-original permanent FaceIds** BEFORE an edit. No geometric
proximity matcher, candidate-distance threshold, or coincident
triangle can infer intent. The returned private opaque declaration
binds the authentic pre-edit M27 lifetime certificate; M27 accepted
successors now seal the exact predecessor certificate and whether
the stage was actually reflected. The M21 reader accepts only a
DIRECT independently proven M24/M25/M26 successor, with the original
first-root anchors and immutable source ownership intact. Forged
counter resets, sibling branches and reanchoring do not inherit
the preceding declaration.

A reflected edit requires the original reciprocal permanent
VertexId involution and source-proven opposite-winding FaceId
partners (including legitimate X=0 singletons); intended FaceIds
must be closed under this reflection. A reflected successor refuses
an asymmetric intent declaration or incomplete partner proof.
A complete M27 7×living-descendant audit is always performed and
preflight bounded, **including the intentionally moved patch**;
only afterward are the explicitly selected original-source witnesses
exempted from the *geometric drift limit*. Unselected original
Exposed/ReversedBoundary outer and cavity walls must retain
original-edge-normalized first-original fidelity. A hidden entire
source may retire ONLY through the independently completed c3/M03
signed-winding Buried/Outside source proof, not by scoped intent.
The decision reports protected samples, authenticated buried
retirement and maximum protected original deviation without
publishing any mesh.

Cross-platform acceptance in PR #73 checks real asymmetric and
reflected X=0 local sculpt/split, real asymmetric and mirrored c3/M03
burial with preserved independent reversed cavities, two wholly
coincident original closed shells with nonaliased source scope,
and stale/forged/incomplete/one-below-budget refusals. No active
terminal worker gate or new desktop behavior is added. M22 must
consume this authenticated scope before original-root CAS and
prove cumulative first-original threshold refusal; M23 and
original M12 remain separate. The sampled one-sided fidelity
limit and default-disabled Merge geometry option are unchanged.

## October 18, 2026 — M22 flat implementation replan; M28 acceptance

M22's original objective is unchanged: **active cumulative first-original
source fidelity immediately before one complete physicalExact worker
CAS**, with deliberate M21 feature scope, protected outer/reversed
cavity surfaces, exact d2 union, all finite work caps and recoverable
atomic refusal. The actual worker performs several PRIVATE
Inflate/refine/collapse/d2 stages in one original-root Step. M21's
opaque intended feature declaration and M27's lifetime certificate
must name the PREVIOUS PUBLISHED root, never a private intermediate
stage. Accordingly the complete remaining sequence was planned as
flat independently-completable **M28 → M29 → M30 → original M22
integrated acceptance** in `todo.md` BEFORE implementation.

M28 (PR #74; `docs/V17_M28_PHYSICAL_STAMP_SOURCE_BUNDLE.md`)
accepts a complete authentic physical stamp source chain: full bounded
unchanged FaceId/VertexId/ordered triangle incidence across normal-
driven Inflate, exact moved VertexId and affected source face halo,
actual committed M24/M25 splits/flips, conservative protected-safe
collapse and optional M26 certified c1/c2/c3/M03/d2 terminal join.
An actually NO-CHANGE collapse consumes no evidence; a real collapse
requires complete source proof. All finite stage work and original
face ownership are replayed against exact predecessor COW roots,
then sealed into ONE externally valid M21 predecessor-bound successor.
First original seven source witness points/edge lengths never rebase.
Genuine two consecutive physical stamps show increasing protected
first-original drift; an actual Inflate-plus-M24 local split and
an actual physically generated source-exact union prove the full
private chain. Forged movement reports, stale roots, exhausted full
vertex/source evidence and one-under caps refuse. GCC, Clang and
MSVC all passed 23 CPU suites and applicable benchmarks.

This remains a **read-only private-stage proof**. M29 must preserve
real worker published lifetime and recoverable history/checkpoint
or fail safely on missing first-root source credentials. M30
must connect complete terminal M21 fidelity and intent to both
actual physical worker CAS paths, including queued input, atomic
rejection and later valid FIFO draining. Original M22 remains OPEN
until combined M28–M30 acceptance; M23/M12, unrelated milestones,
the current PR-only CI cancellation and disabled desktop Merge
geometry remain unchanged.

## October 18, 2026 — V17-M29 authenticated bounded lifetime history

The preplanned independent M29 gate under unchanged original M22
is accepted in PR #75. `RevisionHistory` now optionally retains
source-authenticated FIRST-original M27 lifetimes alongside each
immutable published COW root, verifying the actual direct parent
certificate, root, original source owner/witness graph and cumulative
work before altering undo/redo history. Rejected missing/forged/stale
or sibling lifetime candidates do not discard a redo branch. Undo
and redo restore the exact matched original proof. Opaque
predecessor certificates are now WEAK links: the active lifetime
holds its first ORIGINAL and current terminal source roots, but
evicted intermediate roots can truly retire without being pinned
by every newer certificate. M28 tracks reflected proof flags
while the private stage roots are still authentic and alive.

The existing checkpoint V12 binary remains supported. A protected
source-backed checkpoint writes V13 with a checksummed requirement
marker. The M27 opaque certificate may travel in a trusted in-memory
checkpoint, but **is not fabricated during binary read-back**;
a terminal-root-only file cannot prove its own prior M24/M25/M26
ancestry or original witness geometry. A protected physicalExact
recovered session without separately authenticated first-original
source evidence therefore **refuses before worker startup**.
This is deliberately fail-closed recovery, not evidence that
protected binary editing can resume from an unauthenticated file.
A future positive disk-resume transcript must be independently
verified and cannot reset the original baseline.

Acceptance tests exercise real sequential COW source Steps,
cumulative original-source work, rejected stale/forged/missing
history, undo/redo, branch replacement, bounded eviction,
legacy V12 and protected V13 checkpoint recovery, read-only
original history, and trusted in-memory handoff. Substantive
GCC/Clang/MSVC Actions #39176592637 passed all 23 CPU suites and
applicable benchmarks. See
`docs/V17_M29_AUTHENTICATED_LIFETIME_HISTORY.md`.

M30 still owns the active pre-CAS worker, mirrored collision,
FIFO, final original witness and atomic refusal integration.
The original M22, M23 and M12 remain open; desktop physicalExact
remains default-off and the PR-only CI concurrency policy remains.


## October 18, 2026 — V17-M30 full flat implementation plan and M31 input contract

Inspection after M29 found TWO distinct unprotected publication entry points:
the complete private physical Inflate/refine/collapse/optional d2 stamp and
the separately queued explicit source-exact union. Both still perform CAS
then append history without M29 credentials. M21 has no authenticated
caller-intent channel to those commands, and the M28 full source-bundle
reader by itself does NOT make worker publication protected. The entire
remaining V17-M30 sequence was therefore recorded in `todo.md` as FLAT
independent M31 input authority -> M32 genuine physical final pre-CAS
policy -> M33 explicit queued exact final pre-CAS policy -> M34 integrated
worker FIFO acceptance -> unchanged original M30 gate -> original M22 gate,
before any implementation edits. M23, M12, M13–M15 remain separately open.

M31 supplies deliberate permanent first-original FaceId selections as
literal FIFO Begin/explicit Merge command data, never guessed from brush
proximity or from candidate geometry. A fresh opt-in exact session captures
immutable M19/M27 seven-sample anchors once and keeps the ORIGINAL reflected
ID involution separately from later live mirror pairs. Authenticated trusted
in-memory handoff can retain that first original pairing; unverified
terminal-only disk recovery cannot establish a new original reference.
M21's private original-root declaration checks nonempty caller selection
for original ownership, complete reflection closure, X=0 fixed points and
finite scope work. Bad physical scope refuses the stamp and leaves the same
worker usable; explicit bad union scope refuses before cut, separately from
physical Begin state. Later queued valid commands can continue.

**M31 is only an INPUT contract.** Source ownership is not equivalent to
M28+M21 full-final cumulative protected surface fidelity; production
terminal CAS/history/undo/checkpoint credentials still require M32/M33.
No tolerance threshold, post-d2 final audit or history-proof guarantee
is claimed for these pre-existing worker operations until later milestones
actually wire them. Source-verified scope cannot excuse unclassified
retirement, third-party coincident ownership or erased cavity walls.
M11's sixteen physical positive unions and default-disabled desktop
Merge geometry remain required. See
`docs/V17_M31_ORIGINAL_INTENT_TRANSPORT.md`.


**M31 acceptance checkpoint:** PR #76's substantive
Actions #39179461146 passed the GCC, Clang and MSVC build/tests and
applicable benchmarks. Explicit fresh first-original source scopes
are accepted or refused without promoting a candidate root; bad
physical and explicit union input is recoverable and later real work
drains. M31 is DONE; M32–M34, original M30 and original M22 remain
OPEN. It would be false to call M31 the active terminal fidelity gate.


## October 18, 2026 — V17-M32 physicalExact original-root final gate

The M31 original FaceId input channel and first-root authority alone did
not protect actual published geometry: the real physical worker still
appended untracked history without evaluating M21 on the terminal.
M32 now constructs the true M27 sealed original lifetime **before** M29
history initialization for a fresh `physicalExact` import, rather than
attempting to retrofit a tracked history. A genuinely authenticated
trusted in-memory checkpoint retains its sealed first-root reference
without rebasing. A terminal-only protected V13 disk checkpoint cannot
resume physicalExact without separately proven original ancestry.

The physical worker first prepares normal-driven Inflate, authenticated
M24/M25 local refine and collapse, V13 original source contacts and
optional source-certified c1/c2/c3/M03/d2 TRUE Boolean union privately.
After speculative mirror journal staging and after caller validation,
but **before** the only old-root CAS, M28 independently checks this
WHOLE private source chain, full Inflate FaceId/VertexId census, complete
bounded source evidence and all source-owned surviving/retired final
FaceIds. M28 compresses it to ONE sealed external certificate whose
direct predecessor is the *previous published* root and whose terminal
is the ACTUAL post-d2 COW index. M29 publication verification checks
that direct transition before CAS. M21's previous-certificate-bound
explicit original FaceId declaration then performs the complete bounded
seven-witness-per-original-living-descendant fidelity decision in each
original source's own original shortest-edge units. A caller exemption
only excludes explicitly named originals from the *geometric threshold*,
not from work, ancestry, complete classified c3/M03 retirement, final
d2 weld identity, or protected original/cavity ownership.

A real accepted first small stamp followed by a second individually
small but first-original cumulatively excessive **unintended** stamp
now refuses BEFORE CAS, leaving the last published root and sealed
source counters untouched. One-under full witness and Inflate source
census refuse. An explicit first-root intended scope permits deliberate
movement even at zero protected tolerance, but omission of that scope
on a subsequent stamp does NOT permanently exempt its living original
FaceIds. A fully no-change Step does not append a null lifetime to the
M29 tracked history. Accepted Steps append one exact COW index plus
its sealed one-external-Step lifetime; undo/redo and trusted checkpoint
observe matching certificates, dirty face owner/mirror net deltas and
picker immutable roots.

The genuine M11 sixteen mirrored unions revealed that **per-Step**
intent scope must account for PREVIOUS deliberate deformation of the
same immutable original source. If a former sculpted patch is omitted
from a later request, its persistent earlier deviation from its first
original rightly exceeds the unscoped protected tolerance. The caller
therefore explicitly continues its former intended FIRST-original
source set together with the next region. Before M21 declaration,
a previously nominated original now wholly retired is omitted from
the ACTIVE intent only when its authenticated lifetime explicitly
carries an M26 c3/M03 Buried/Outside retirement; any uncertified
disappearance refuses. The otherwise still-live previously intended
patch must be explicitly re-declared on that Step. New future tower
patches remain untouched and protected. No blanket proximity, global
XYZ-matched lifetime, threshold inflation or implicit permanent scope
union is used. This preserves all sixteen ACTUALLY JOINED tower Steps,
their FIFO clean/refusal/recovery and mirrors while maintaining the
first-original policy on all sources outside the caller's scope.

M32 deliberately does NOT certify the independently queued explicit
source-exact union (M33): a tracked history safely refuses that command
BEFORE CAS rather than attempting a credential-free history append
AFTER publication. Untracked existing explicit union paths remain
unchanged. The old V16 `anchoredSurface` and diagnostic hysteresis are
not substitutes for this terminal gate. M14 still owns exceptional
post-CAS injected faults, and no desktop Merge geometry option is
enabled before M15. The M32 policy is finite *one-sided* seven-witness
evidence, not a globally exact CCD or continuous Hausdorff promise.

See `docs/V17_M32_PHYSICAL_TERMINAL_POLICY.md`. Substantive
[Actions #39182191436](https://github.com/logiclrd/Mesher/actions/runs/39182191436)
passed GCC, Clang, MSVC warnings-as-errors, all 23 CPU test suites
and applicable benchmarks. M33, M34, unchanged M30/M22, M23/M12,
M13–M15, V18 and V19 remain open.


## October 18, 2026 — V17-M33 exact queued original-source pre-CAS gate

**Accepted PR #78**: the explicit `AsyncSculptWorker::mergeExactContacts`
command already carries a complete explicit contact set, old immutable
expected root and M31 FIRST-original FaceId nominations. Until M33,
M32 necessarily refused such commands on authenticated M29 history,
because a source-less append after CAS could not meet M29's immutable
predecessor certification contract. M33 adds the missing full terminal
source proof rather than weakening history.

The exact queued worker still prepares all c1/c2/c3/M03/d2 local
operations in ONE private `ExactCutStep`, with its final welded index
the ONLY CAS target. It checks existing net original mirror
retirement/insertion and caller validator before speculative changes.
If the session is source-authenticated, it requires the actual
M29 `currentFidelity()` and matching `expected` published COW
root; they must retain the exact same FIRST-original anchor root as
the session authority. The M26 `advanceOriginalPatchLifetime`
recomputes complete cut child ancestry, classified source ownership,
whole-source Buried/Outside retired originals, final d2 stable FaceId
and mirror journals, and cumulative evidence against the exact old
original source certificate. Current OLD-root live mirror IDs are
supplied for exact-cut validation; the unchanged FIRST-original
mirror map independently authenticates M21's caller declaration
including X=0 self-mirror orbits. Never infer source from coincident
XYZ or replace anchors with the terminal.

The caller's raw original FaceId count is bounded BEFORE allocation
or filtering. Duplicate/foreign IDs and missing active originals
without independently M26-sealed whole-source retirement refuse.
As with M32, caller scope for each Step must explicitly continue
to name earlier intentionally moved still-live original faces; a
now-certified entirely retired original may be excluded only from
the active M21 declaration. `evaluateOriginalPatchIntent`
checks the COMPLETE bounded seven-witness-per-certified-living-child
surface against first-original local edge scales, with only
explicitly named patches exempt from the *geometric* deviation
threshold. All sources remain subject to ancestry, work, cavity and
classification checks. A source-disabled/unverified V12 recovered
root cannot be promoted to `physicalExact` via a queued union, and
a V13 protected disk checkpoint cannot fabricate first-root proof.

The NEXT sealed source successor is verified against BOTH the
configured M21 lineage bound and the actual M29 append's immutable
default proof cap BEFORE either controller mirror journal begins
or the ONE expected-root CAS runs. Source, policy, malformed input,
budget or stale refusal leaves the existing indexed root, undo/redo
branch, mirror controllers, dirty partitions and checkpoint proof
untouched. Only successful final publication updates Inflate's
mirror net, appends the single source-matched M29 history entry,
updates counts, and gives renderer/picker the terminal COW root
rather than a private cut. Recovery of in-memory authenticated
history preserves the immutable original certificate; V13 disk
remains fail-closed for source-backed edit resumption.

Real queued asymmetric supported original unions are accepted after
an invalid source request; one-under FULL original descendant
witness tests, one-under complete M26 cumulative face work,
raw source nomination bound, and foreign/stale expected inputs
refuse atomically. A complete THREE-source-contact protected X=0
original input accepts one joined terminal with valid singleton and
reciprocal mirrored orbits; partial contacts and half-orbit caller
scope refuse privately. Undo/redo restores matching roots,
original-source proof and mirror maps; a refused alternate union
does not discard redo. The prior 23 CPU suites, untracked
explicit-queue behavior, and sixteen M11 true physical unions
remain green. See
`docs/V17_M33_EXPLICIT_QUEUED_ORIGINAL_POLICY.md` and
[Actions #39184683170](https://github.com/logiclrd/Mesher/actions/runs/39184683170)
for full GCC/Clang/MSVC warnings-as-errors and applicable benchmarks.

M34 remains the independent COMBINED physical-stamp/explicit
queue 4D FIFO acceptance milestone. Original M30 and M22 are
still NOT checked; M23, original M12, M13–M15, V18/V19
and M14 injected post-CAS exceptional atomicity remain open.
The desktop Merge geometry UI remains default disabled.


## October 18, 2026 — V17-M34 integrated authenticated worker FIFO acceptance

The first three independently complete flat peers of original M30
delivered M31 caller FIRST-original FaceId intent, M32 certified
physical exact final source/intent/history CAS, and M33 certified
queued c1/c2/c3/M03/d2 exact terminal CAS. The M34 regression uses
BOTH publication entry points on ONE real `AsyncSculptWorker`, not
two stand-alone private simulations with fabricated publication.

Its imported composite has genuinely intersecting tetrahedra plus
an independent closed octahedron far from the intended union. A
physical stamp against the unjoined tetra configuration correctly
refuses: V13 sees genuine unresolved ORIGINAL source contacts, and
the unrelated local stamp must not claim to make them clean. The
integration consequently starts with an explicitly nominated
FIRST-original tetra source set and TWO queued exact unions with the
SAME expected root. The first has a genuine one-under transaction
bound and refuses. The second performs the complete M26 certified
source c1/c2/c3/M03/d2 join, followed by full M21 seven-witness
terminal work and M29 direct history verification, yielding precisely
one terminal COW root and one GPU dirty face-owner set. Older
picker/BVH readers and original FaceIds remain immutable.

From THAT accepted joined root, the test independently computes
two normal-driven physical post-union proposals and audits their
FIRST-original octa source descendants, using the complete
seven-witness-per-LIVING-CHILD budget (which may exceed seven times
the number of unsplit source faces). The protected threshold falls
strictly between the two cumulative original edge-relative
deviations. The actual worker's first 4D Begin/End nominates the
still-intended tetra original source set but leaves the octa
UNINTENDED, and passes within that threshold. The identical second
physical stamp refuses cumulatively BEFORE CAS, without changing
history/dirty/root. A stale prior queued-union expected pointer
also refuses, and a later 4D Begin/End deliberately nominates the
octa and previously intended source set to show real valid FIFO
progress after both kinds of refusal. Undo/redo reverses and replays
THREE exact root identities (source-exact / physical / physical);
a checkpoint holds ONLY the terminal welded geometry while V13
disk-only recovered edits remain fail-closed absent independent
first-root source authentication.

The M33 TRUE three-contact X=0 fixed-point protected queued original
union was extended with a genuine symmetric physical 4D Step on the
SAME worker and directly subsequent undo/redo. The original
self-mirrored and reciprocal ID involutions remain separate from
post-union live mirrors, so accepted and rejected input cannot
manufacture a second coincident seam vertex. All prior M11 SIXTEEN
positive physical reflected unions and source-proven independent
coincident-sheet/reversed-cavity refusal regressions remain in the
same 23-suite CI acceptance. This retains finite original source
work, independent FaceId owners, actual full terminal d2 topology,
and first-original edge-relative protected policy across both APIs.
It does NOT assert a continuous Hausdorff bound or arbitrary
coincident imported surface Boolean.

**Actual discovered concurrency defect (M34 fix).** A producer's
`begin()` reserves through `reserved_.exchange(true)` *before*
its queued Begin is processed. The worker previously cleared
`reserved_` at the end of ANY idle iteration where the flag
happened to be true, even an unrelated queued Merge refusal.
That raced with an arriving Begin and allowed an older forced
snapshot to be mistaken for the completed NEW root, while the
new stamp/history actually continued privately. The M34 integration
caught `history.undoDepth=3` with a stale mailbox of
`completedStamps=2`. A **worker-owned** `workerOwnsStartedStroke`
token is now set only after the real `startTimed` has succeeded;
the worker clears the reservation only once THAT stroke is
inactive with no pending samples, **after** forced publication of
its final COW root and accumulated dirty owner partitions. A
not-yet-consumed Begin can no longer be released by some unrelated
previous queue command. The test asserts snapshot.index matches
the actual accepted next lifetime rather than weakening renderer
acceptance to permit a stale frame.

See `docs/V17_M34_INTEGRATED_PROTECTED_FIFO_ACCEPTANCE.md`.
[Substantive Actions #39187712935](https://github.com/logiclrd/Mesher/actions/runs/39187712935)
passed Linux GCC/Clang, Windows MSVC warnings-as-errors, all
23 CPU test suites and applicable 100K benchmarks. M34 alone
is checked; **original M30, original M22, M23, original M12,
M13–M15, V18/V19 remain OPEN**. M14 post-CAS injected
exception rollback and M15 desktop Merge geometry default-disabled
opt-in are separate, not implicitly accepted here.


## October 18, 2026 — Original V17-M30 final full-terminal pre-CAS acceptance

**Original M30 is accepted, not merely the four M31–M34 flat
implementation peers.** Fresh genuine FIRST-original FaceId
authority, exact immutable source anchors and per-face
original edge scales are carried across BOTH real worker
publication paths. The physical M28 private Inflate/
refine/collapse/optional c1/c2/c3/M03/d2 chain yields ONE
external original-root successor; the independently queued
exact-cut M26 c1/c2/c3/M03/d2 path yields another. Both
authenticate the precise previous M29 source certificate
and apply the full M21 bounded seven-per-living-descendant
first-original terminal audit *before* CAS. Intent is a
deliberate caller-scoped FIRST-original FaceId declaration:
only the geometric threshold is waived for explicitly
named original owners. Whole-source retirement still
needs certified Buried/Outside classification, no
coincident XYZ owner inference, and intact X=0
self-mirror/reciprocal orbits.

M30 inspection closed one final mismatch: M29 history
append itself always verifies the immutable DEFAULT
source-lifetime bound, whereas physical M32 had previously
preflighted only the caller CONFIGURED bound. M30 adds
the same default transition verifier before physical
root CAS that M33 already runs for queued exact unions.
An accepted genuine physical transition now verifies
the actual M29 default proof even with a larger allowed
caller descendant bound. This avoids the known class
of history-proof failure after a source-accepted CAS.
General injected post-CAS exception atomicity remains
the separate open M14 goal.

M30 CI also exposed an intermittent false-positive
in M34's mixed-worker wait: physicalExact refusals may
increment `rejectedUnions`, so an absolute
`waitForUnion(worker,3)` was not a fence for an
*additional* queued stale-root Merge. The revised
test captures the outcome baseline after the
physical refusal, waits for ONE further explicit
result, and checks the specific stale-root reason,
unchanged history/publication and empty renderer
mailbox before the next physical Begin. This is
a test-harness synchronization fix, not a delay
or a relaxation of the worker acceptance contract.

Evidence: M32 first permissible normal-driven movement,
cumulative rejected second, full/one-under M21 and
Inflate evidence, explicit zero-threshold intended
movement; M33 true queued asymmetric and three-contact
mirrored X=0 source-proof unions, one-under M26/
M21 work, stale roots, history/checkpoint; M34
single-worker real mixed exact/physical FIFO, true
terminal dirty mailboxes, X=0 successor, and
three exact undo/redo roots; M11 sixteen genuine
mirrored physical unions, independent coincident
FaceId and protected reversed-cavity tests.
[Actions #39192742674](https://github.com/logiclrd/Mesher/actions/runs/39192742674)
passed Linux GCC/Clang and Windows MSVC warnings-as-errors,
the full 23 CPU suites and applicable benchmarks.
See `docs/V17_M30_ORIGINAL_TERMINAL_ACCEPTANCE.md`.

Only the original M30 checkbox is closed here.
**The original M22 joint acceptance, M23 and original M12
remain OPEN**, as do M13–M15, V18/V19 and M14
fault-injected post-CAS guarantees. No desktop
Merge geometry option has been enabled.


## October 18, 2026 — Original V17-M22 joint cumulative terminal acceptance

**The ORIGINAL M22 checkbox is now accepted as a separate
criterion-by-criterion review of M28–M30**, not checked merely
because its flat implementation peers were complete.
The independently authenticated M28 complete PRIVATE source
transition, M29 original-root/previous-published certificate,
and M30 dual physical/queued original-source M21 complete
seven-witness-per-living-descendant, original-edge-scaled
terminal decision compose at the only actual root CAS.
Both physical and queued paths also preflight the
immutable default M29 proof limit before publishing.

M32's actual allowed first physical drift and atomic
rejected cumulative second drift; M33's complete
source-owned queued union, original FaceId/retirement,
one-under full work and stale-root refusals; M34's
genuine single-FIFO exact→physical→refusal→physical
history/snapshot/checkpoint and X=0 reflected successor;
and all sixteen genuine M11 positive reflected unions
remain green. M22 strengthened reflected original
atomic refusal specifically: following an accepted
THREE-contact X=0 queued original union, a deliberately
HALF-orbit source-scoped physical command refuses
without changing the welded COW root, current sealed
source lifetime pointer, first original anchor,
live singleton/paired mirror map, history depths
or dirty owner set. The exact original/terminal
reflected mirror history still undoes and redoes.

Substantive [Actions #39197216458](https://github.com/logiclrd/Mesher/actions/runs/39197216458)
passed GCC/Clang/MSVC warnings-as-errors, all 23
CPU suites and applicable benchmarks. See
`docs/V17_M22_FINAL_INTEGRATED_ACCEPTANCE.md`
for the original M22 evidence matrix.

M23 conclusive Clean/Contact episode decisions, original
M12 combined exposed-feature/hysteresis acceptance,
M13 telemetry, M14 post-CAS injected exception
recovery, M15 explicitly disabled-by-default
desktop Merge geometry opt-in and V18/V19
remain UNCHECKED. Do not read this as a continuous
Hausdorff, all-mesh Boolean or CCD theorem.


## October 18–19, 2026 — V17-M23 conclusive Contact/Clean episodes

**M23 accepted in PR #82**, substantive
[Actions #39200600909](https://github.com/logiclrd/Mesher/actions/runs/39200600909)
green on GCC/Clang/MSVC warning-as-errors, 23 CPU suites and
applicable benchmarks. The old diagnostic tracker could
falsely close a contact episode after two valid Clean
checks, an Unknown, and one more Clean, because Unknown
did not reset `quietRun`. This is corrected: an unknown,
exhausted or refused assessment resets the consecutive
Clean and Contact streaks without falsely terminating
the ACTIVE episode. Exactly three subsequent conclusive
Clean decisions close it and increment a distinct
`closedEpisodes` diagnostic count. New Contact
before closure extends the same episode; Contact after
closure starts a new one.

The worker's physicalExact original-root V13 and legacy
selected V16 full candidate assessment now stage one
diagnostic observation until downstream candidate
validation and FIRST-original M21/M29 terminal checks
resolve. Only a fully classified, otherwise accepted
Clean can advance `quietRun`; an independently proved
Contact may remain informative even if its later weld
fails. Rejected invalid caller FIRST-original FaceId
input and V13 source budget exhaustion count Unknown,
not Clean. This state NEVER authorizes a weld or omits
a new V13/c1/c2/c3/M03/d2/final source/geometry audit.

The regression performs ONE real 4D mirrored physical
source union from clean separate M11 tetra towers,
then TWO actual completed and clean V13 exterior
deformations, rejects a foreign source scope as
Unknown, then requires THREE more separately
classified/published Clean deformations before
the episode closes. A separate real second-tower
physical union subsequently recontacts and opens
episode two. Each dirty renderer mailbox is consumed
between accepted Steps to distinguish coalesced
prior dirty partitions from refusal-induced dirty
changes. M11's actual finite-source budget exhaustion
regression now checks unknown classification and
proper recovery contact. The older V16 test was
updated from its contradictory interrupted-quiet
expectation. All sixteen actual M11 positive
reflected source unions remain green.

See `docs/V17_M23_CONCLUSIVE_CONTACT_EPISODES.md`.
Only the ORIGINAL M23 checkbox is closed; original
M12 plus M13–M15, V18/V19 are still unchecked.
No M14 exceptional post-CAS or desktop M15 opt-in
claims are inferred.


## October 19, 2026 — V17-M35 repeated local physically triggered source unions

Original M12 was intentionally NOT checked after its M19–M23
component acceptance. M35 adds the missing *positive* repeated
actual-union evidence in overlapping sculpt support: two real
closed reflected tetra contact bands, originally disjoint,
whose local six-unit brush spheres select some of the SAME
original stable source vertices. Two sequential physical
normal-driven Steps on ONE authenticated `physicalExact`
brush produce independently certified, source-owned full
c1/c2/c3/M03/d2 exact joins with a cumulative first-original
M21 post-terminal audit. Two distinct immutable published
roots, two history entries, fully reflected live mirror
identities, old BVH pointer stability, correct dirty
partition publication and exact two-step undo/redo pass.
Both Contacts belong to one active diagnostic episode.
No source proximity fallback or classifier permission
cache is introduced.

[Substantive Actions #39204571996](https://github.com/logiclrd/Mesher/actions/runs/39204571996)
passed GCC/Clang/MSVC and all 23 CPU suites plus applicable
benchmarks. See `docs/V17_M35_REPEATED_LOCAL_PHYSICAL_UNIONS.md`.

M35's local overlapping brush support is not yet a theorem of
same-component 3-body joining. M36 still must combine deliberate
deformation and first-original protected outer/reversed-cavity
walls, finite exact/one-under budgets, actual episode
Clean/Unknown and FIFO recovery with these real positives.
**Original M12 is OPEN**; M13–M15 and V18–V19 remain untouched.

## October 19, 2026 — V17-M37 protected inward cavity in true same-local physical unions

M36 has been explicitly replanned **in advance** to independent flat
M37 → M38 → M39 → ORIGINAL M36 → ORIGINAL M12 final acceptance.
M37 (PR #84, substantive Actions #39208440042, GCC/Clang/MSVC green)
extends the M35 physical-positive search, preserving its two genuinely
normal-driven source-certified exact unions in one overlapping local
brush influence neighborhood. The same first-authenticated source now
contains three closed mirrored tetra bands plus a separate distant
mirrored outward octahedral shell enclosing a real inward-wound
tetrahedral cavity on each side. Source signed-winding queries prove
the proper 0-void / +1-material / 0-outside cavity topology.
The two actual local contacts are performed with explicit renewed
caller original FaceId intent restricted to only 32 of 72 original
faces. No nearest-XYZ or global mirror deduplication selects owners.
Both c1/c2/c3/M03/d2 physically generated joins create distinct
M29 COW/history terminals; forty unscope-protected original exterior
and reversed-cavity FaceIds retain exact ordered incidences and
vertex positions. Complete first-root living-descendant audit,
authenticated certificates, reciprocal reflected live IDs, distinct
dirty owners, old picker readers and exact double undo/redo pass.
Separate attempted exterior and cavity-wall bilateral deformations
both refuse without changing terminal COW pointer, sealed lifetime,
full mirror pair map, history, publication or dirty owners.

This is NOT a new universal same-component Boolean or an X=0 fixture.
M38 has distinct complete/fenced source work plus cumulative first
root drift, M39 true same-local 4D Contact/Clean/Unknown/FIFO recovery,
and ORIGINAL M36 the simultaneous final combined policy. M12 remains
open for its own separate acceptance; M13–M15 and V18/V19 are not
claimed. See `docs/V17_M37_SCOPED_LOCAL_CAVITY_ACCEPTANCE.md`.
