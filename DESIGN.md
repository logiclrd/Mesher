# Mesher design principles (future milestones)

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
