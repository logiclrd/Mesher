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
