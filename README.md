# Mesher

An experimental **C++20/Vulkan** mesh editor foundation. The application generates a random-point triangulated sphere, animates a
symmetric Inflate-brush demonstration by default, and displays each stamp in a
full-screen 3D viewport with flat face lighting, a 30%-gray wire overlay, and
a gridded XY floor. **Z is up.** The geometry engine is independent of Vulkan/GLFW and can
be tested without a graphics device.

## Build

Requirements: a C++20 compiler, CMake 3.22+, Vulkan SDK/development headers,
GLFW 3.3+, and either `glslc` or `glslangValidator` (for compiling the included
GLSL shaders). On Ubuntu/Debian, install `libvulkan-dev`, `libglfw3-dev`,
`glslang-tools`, `cmake`, and `g++`. A Vulkan-capable graphics driver is required
to run the viewport.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher
```

For CPU-only builds, with no Vulkan/GLFW dependencies:

```sh
cmake -S . -B build-cpu -DMESHER_BUILD_VIEWPORT=OFF
cmake --build build-cpu --parallel
ctest --test-dir build-cpu --output-on-failure
```

## Command line

```text
Mesher [--symmetric|--asymmetric] [--vertices N] [--seed N] [--relax N]
       [--no-inflate] [--step D] [--strength D] [--segments N] [--brush-radius R]
       [--stamps-per-second HZ]
```

Defaults are `--symmetric --vertices 5000 --seed 1 --relax 65`. The viewport respects the window manager's display placement. **Escape** exits.
**Space** pauses/resumes, **Right** applies one stamp while paused, and
**R** restores the original sphere and replays the animation.

By default Mesher inflates two mirror-image half-circle strokes across the
front of the generated sphere, with spherical influence radius **0.4** and
hardness **0.3**. The default stroke Step is **0.02** mesh units, strength is
**0.012** mesh units per full-influence stamp, and each arc is approximated
with **128** input segments. `--no-inflate` displays the unmodified sphere;
`--step`, `--strength`, `--segments`, and `--brush-radius` allow experimentation. The demo is
precomputed before the viewport starts (not interactive yet).

- **Asymmetric:** Uniformly distributed random samples on the unit sphere,
  followed by iterative tangential repulsion using a spatial grid.
- **Symmetric:** Compute
  `m = min { integer > 2π / sqrt(8π / (n sqrt(3))) : (n-m) is even }`.
  Generate `m` points on a unit circle with randomly initialized spacings,
  relax the gaps toward equal spacing, then fix that circle. Generate
  `(n-m)/2` random hemisphere points and relax them against one another,
  the fixed ring, and a mirrored ghost population. Reflect the hemisphere's
  vertices and triangles, sharing the ring so there is **no doubled seam**.
  The initial reflection plane is `X=0`: its normal is perpendicular to the
  camera direction, and the ring lies in the `YZ` plane.
- **Triangulation:** An incremental 3D convex hull triangulates the relaxed
  points without Catmull–Clark smoothing. For symmetric meshes, each half is
  triangulated separately and the planar cap is discarded before reflection.
  Outward winding and manifold topology are verified.

For 5,000 vertices the result has **9,996 triangles** (Euler characteristic 2).

## Rendering

The view looks toward the origin from negative Y at a slight elevation, with
an XY grid at `Z=-1.22`. The camera is perspective, with 45° vertical field of
view, and its distance is recalculated on viewport size changes so the sphere's
projected diameter is 2/3 of the **smaller viewport dimension**. One point light
is above/behind/to the camera's left, and a second is to the far right. Faces
are lit separately (flat shaded), and unique mesh edges are drawn as
`RGB(0.30,0.30,0.30)` lines with depth testing. The floor has a second
material. The viewport rebuilds Vulkan swapchain-dependent resources on
resolution changes.

## Brush foundation

`include/mesher/brush.hpp` defines `BrushArea`, `Brush`, `BrushStroke`, and
`InflateBrush` independently of Vulkan. A stroke starts with one or more
`(center, radius, hardness)` spheres and samples their combined **maximum**
influence. Every center has full influence through `hardness × radius`, then
falls to zero at `radius` according to complementary quintic *smootherstep*.
Hardness is a fraction in `[0, 1]`.

`BrushStroke::start` stamps immediately. `update` accepts new positions for
exactly the same number of centers, validates that each has travelled the same
3D distance, and samples **all** centers simultaneously at constant arc-length
`Step`. Leftover fractional steps carry across updates, including segment
corners. `finish` processes the last update, stamps the terminal position if
not already sampled, and releases brush state. `InflateBrush` moves influenced
vertices along the **normalized sum of the unit face normals** of incident
triangles, evaluated once at the beginning of the stroke by default.
This avoids a feedback loop in which heavily deformed face normals turn inward,
folding triangles across the symmetry plane under wide overlapping strokes.
The brush now uses live face normals again: these are recomputed from the
modified mesh on every stamp. This deliberately exposes the folding effect
for diagnosis. Normal strength is a displacement **per stamp**.

This milestone intentionally keeps existing topology unchanged and performs
no adaptive subdivision, decimation, brush picking, interactive painting,
undo, or acceleration for hundred-million-vertex meshes. The Inflate demo
is deliberately small and deterministic, for validating the stroke engine.

## Architectural notes and limitations

- The generator returns a simple `Mesh` containing double-precision vertex
  positions and indexed triangles; it has **no dependency on Vulkan**.
- Topology validation checks vertex indices, outward winding, edge incidence,
  connectivity, and the sphere's Euler characteristic.
- Random generation is seeded and reproducible within the same standard
  library implementation; C++ distributions are not guaranteed to give the
  same sequence on different toolchains.
- The hull algorithm currently scans active faces for visibility, making
  it approximately **quadratic** in the number of vertices. This is suitable
  for the initial 5,000-vertex baseline, **not** yet the planned many-million-
  vertex editing engine. Replace it with an accelerated robust hull/meshing
  algorithm when scaling up.
- CPU-visible vertex buffers are refreshed after each animated stamp for the
  small diagnostic demo. Efficient dirty-region GPU updates, persistent
  device-local buffers and staging remain future work.
- Interactive sculpting, painting, selection, undo, mesh import, and local
  remeshing are not yet implemented. The animated Inflate driver demonstrates the brush-stroke foundation without
  changing triangle connectivity. Interactive mouse picking remains future work.

## Status

The CPU geometry and topology tests run without a GPU. The Vulkan viewport
requires a machine with Vulkan/GLFW development packages to build and an
actual Vulkan implementation to run. See the GitHub Actions workflow for the
Linux full-build test configuration.
### Wide-brush seam regression

The equatorial ring is shared by vertex **index** across both hemispheres.
With live per-stamp normals and overlapping radius-0.4 brush influences, the
mesh can nonetheless fold through its reflection plane. The experiment is now
played in the Vulkan viewport at **10 stamps per second**, initially showing
the undeformed sphere. Every stamp uploads updated solid and wireframe data;
the window title reports stamp progress and the number of radially inverted
triangles. Inversion-count changes are logged to standard output.

**Space** pauses/resumes playback, **Right** single-steps while paused, and
**R** resets and replays. The speed can be changed with
`--stamps-per-second HZ`. The animation is intentionally destructive at
large brush sizes so that the failure can be diagnosed; there are no safety
guards against self-intersection yet.

The path is recorded using the ordinary distance-sampled `BrushStroke`
engine and applied one stamp at a time by `InflateDemoAnimation`, whose
`advance()` method is independent of the Vulkan renderer. An animated run
and a synchronous run yield identical final positions (CPU regression).

## Experimental directional edge collapse

The default animated Inflate brush remains **unchanged**: it recomputes normals
at every stamp and performs no topology edits. Add `--collapse` to run an
experimental, conservative remeshing pass immediately after each stamp:

```sh
./build/Mesher --collapse --minimum-edge-ratio 0.65 --max-collapse-pairs 12
```

An edge becomes a candidate **only if it shrank during the current stamp**
and is now shorter than the configured threshold. The threshold is a fraction
of the nominal initial sphere edge length, not a request to normalize the
mesh globally. Candidates are tried shortest-first. Every collapse is subject
to the 2-manifold edge-link condition, outward-face noninversion for
previously outward faces, and closed-sphere topology validation. The two
reflected collapses are atomic when symmetry is enabled; the seam is shared,
never duplicated. Midpoint placement deliberately makes this an experiment
in whether simplification smooths the developing pinch, rather than a
geometry-error-optimized production simplifier.

The viewport animation title includes vertex count, shrinking edges below the
threshold, removed vertices, minimum edge length, minimum triangle altitude,
and inverted faces. Pause with Space and use Right for one-stamp diagnosis.
R resets **both** vertex positions and triangle connectivity.

A repeatable headless comparison (no Vulkan window) prints CSV rows for both
modes on the **same initial sphere and stamp sequence**:

```sh
./build/Mesher --compare-remesh > comparison.csv
```

The `--minimum-edge-ratio` default is 0.65 and
`--max-collapse-pairs` is 12 per stamp. Set `--max-collapse-pairs 0` to
disable removals while retaining diagnostics.

**Limitations:** This is deliberately edge-collapse-only, with no splitting,
vertex recolouring, geometric error metric or collision detection. It cannot
fix a valley when the opposing patches approach without shrinking their own
edges. The first version rescans the mesh and checks trial topology on CPU,
so it is not intended for high-resolution scan editing. The nominal spacing
is defined for the generated unit sphere, not arbitrary imported meshes.

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

## Warnings as errors

All Mesher-owned C++ targets, including the Vulkan application, mesh library,
CPU tests, and performance benchmark, compile with `-Wall -Wextra -Wpedantic
-Werror` (GCC/Clang), or `/W4 /WX` (MSVC). This is enforced by CMake and
therefore by CI, in Release and Debug builds. Warnings from third-party
prebuilt libraries are not affected. New compiler diagnostics must be fixed
rather than accumulated as ignored build warnings.


## Compiler compatibility CI

GitHub Actions runs three independent, parallel build jobs:
**GCC** (Ubuntu), **Clang** (Ubuntu) and **MSVC** (Windows, Visual Studio
2022). All three build the C++20 mesh library, full Vulkan/GLFW executable,
shader assets and CPU test suites with warnings treated as errors
(`-Wall -Wextra -Wpedantic -Werror` on GCC/Clang; `/W4 /WX` on MSVC).
Windows fetches GLFW and Qhull through vcpkg and installs the Vulkan SDK.
The long-form 100k performance benchmark and geometry comparison artifacts
run under GCC; MSVC also runs the 100k Quickhull benchmark, while Clang
runs all CPU tests. The jobs fail independently and run in parallel so
platform-specific compiler diagnostics cannot be hidden by a successful
build with another compiler.


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

## Viewport rotation and translation (V17-M53/M55/M60/M61)

Hold **right mouse** to rotate the displayed scene using an
**upright turntable**: horizontal movement changes yaw about original
world **Z-up**; vertical movement changes pitch about fixed display
**X**. The camera can reach a true **90-degree top-down or bottom-up
view**, accounting for the initial ~18-degree elevation; it does
not rotate *past* a pole and invert world Z-up. At an exact pole
Z-up has no screen projection, but horizontal yaw remains defined.
Circular pointer gestures never accumulate roll or invert the view;
release freezes the view. If the RMB gesture begins over
a source surface, the exact immutable-BVH hit is the fixed pivot.
If it begins over background, the ray instead intersects a plane
**parallel to the screen**, through the **currently displayed,
rotated/translated mesh centroid**. This replaces the M53
unrotated fixed-world-Y plane. Parallel/behind-eye ray misses use
the finite centroid as a fallback.

Hold **Ctrl when first pressing RMB** to dolly instead of orbit:
horizontal mouse movement shifts the camera forward/back along
the **centre-viewport** direction; vertical movement does nothing.
Hold **Ctrl when first pressing LMB** to grab-and-drag the **world**
in the screen plane: moving the mouse right/down pulls the displayed
scene right/down (the camera moves oppositely). Normal LMB
continues to sculpt or nominate Merge original faces. The modifier
and gesture mode are latched at mouse-down; pressing Ctrl partway
through an ordinary brush stroke or orbit does not switch action.
**Alt is reserved for a future brush-inversion modifier** and
no longer triggers a camera gesture.
At viewport edges during a camera gesture, the cursor wraps to
the opposite edge for uninterrupted motion, suppressing fake warp deltas. On platforms refusing
GLFW cursor warps (some Wayland compositors), the drag still works
without wrapping and prints a diagnostic. Normal mouse use is
never wrapped.

All transforms are visual only, and inverse-transform the
ordinary sculpt/Merge picking rays: no source FaceIds, worker
mesh or checkpoint are changed. See
\`docs/V17_M55_SCREEN_PARALLEL_ALT_DOLLY_CURSOR_WRAP.md\` and
\`docs/V17_M60_UPRIGHT_TURNTABLE_ORBIT.md\` and
\`docs/V17_M61_CTRL_CAMERA_AND_POLES.md\`.
True Vulkan-driver interaction on both OS families remains M52;
the suspected accepted-union interior edges remain M54 and are
not deemed clean by UI or worker accepted counters.

## Correct hidden-edge wireframe occlusion and W solid-only view (V17-M56)

The previous wireframe shader intentionally moved every line toward
the camera by a large fixed clip-depth offset. This could let edges
from surfaces *behind* the visible volume pass the Vulkan depth test,
as observed in the reflected Merge fixture. The fix removes that
offset; solid triangles still write the depth buffer first, and
wireframe lines must pass the same `LESS_OR_EQUAL` depth test
without writing depth. The depth attachment already prefers
32-bit float (24-bit fixed fallback), so this was a biased-line
problem, not intrinsically an 8- or 16-bit Z-buffer.

Press **W** to toggle the wire overlay while keeping the solid
geometry, viewport orientation, immutable source revision,
history and protected Merge operation untouched. The window
title and console indicate the current wire state. This helps
distinguish display-only edges from real source surfaces.

M54 separately audits whether the accepted reflected union is
actually manifold and has no interior fragments. M52 still owns
actual Linux/Windows Vulkan host rendering acceptance, including
possible rasterization differences for coplanar edges now that
the excessive shader depth pull is gone. See
`docs/V17_M56_WIRE_DEPTH_OCCLUSION.md`.

## Experimental live Merge geometry — explicitly authorized, default OFF (V17-M48)

The standard Mesher Vulkan launch still uses the existing **Mouse Inflate**
worker. No background geometric union is performed. For an *experimental*
first-original-authenticated, finite-budget Merge-capable session, launch
a **fresh** in-memory mesh using `Mesher --merge-geometry`.

For repeatable **experimental source-positive acceptance**, launch
`Mesher --merge-geometry --asymmetric --merge-test-tetra`.
This explicitly replaces the generated sphere with the same two intersecting,
first-original independent asymmetric tetrahedra used in M47/M49/M50 CPU
regressions. It is a deliberately labeled developer test fixture, **not an
importer or a claim of general mesh support**. This requires both flags:
ordinary startup cannot select this source accidentally. The graphical Merge
mode still starts OFF, source FaceIds are never auto-nominated and no Boolean
occurs until an explicit user selection and Enter. Some original tetrahedral faces are occluded from a single view.
In these **explicitly named fixtures only**, press **M** to enter Merge ON,
then press **A** *once* to deliberately nominate the complete finite
FIRST-original stable FaceId list (8 asymmetric faces or 16 reflected).
Review the HUD selected count, then press **Enter** to request the
single bounded worker Merge. You can still click visible faces separately;
A adds only missing IDs without duplicating a clicked source.
It is only available while the exact *initial immutable test-fixture*
revision is displayed; subsequent roots, normal generated spheres and
unarmed sessions cannot use this shortcut. A is a deliberate user action,
not source auto-detection, geometry scanning, or automatic merging.

For the genuinely reciprocal reflected two-group developer positive use
`Mesher --merge-geometry --symmetric --merge-test-reflected-tetra`.
The fixture-only camera frames both source groups using exactly the same scale\nfor Vulkan drawing and immutable-root ray picking; normal sphere startup\nretains the existing unit camera. The 16 source vertices and 16 source faces represent two independent
positive-X tetrahedra and their separate negative-X mirrored partners,
with reversed winding. The worker consumes a genuinely sealed
FIRST-original root and eight independently constructed reciprocal
stable-ID mirror pairs, not a sphere-specific nearest-vertex map.
Press **M**, **A**, then **Enter**; wait for actual accepted/refused
worker diagnostics. **Ctrl+Z** and **Ctrl+Y** undo/redo its one
original-root accepted Step. Real Linux/Windows GPU observations remain
an M52 requirement, not evidence obtained from a CPU-only test.
This flag explicitly provisions the protected original-source worker;
the visible Merge editing **mode still starts OFF**. Attempts to combine
`--merge-geometry` with demo/comparison or `--recover` are rejected:
a disk V13 recovery cannot silently invent the opaque M29
first-original source certificate. Unlike an unarmed normal launch,
even Inflate in this specially provisioned session uses the opt-in
protected original-source physics policy.

While in that authorized session:

- **M** toggles Merge mode ON/OFF. The GPU HUD and window title show it.
- **OFF:** normal left mouse strokes select Inflate rather than Merge.
  A launch *without* `--merge-geometry` retains the original legacy path.
- **ON:** left-click an indexed surface to nominate the stable
  `FaceId` returned by the currently displayed immutable revision.
  These are explicit *first-original* nominations, not coordinate-nearest
  equivalence; unknown/retired/or non-original IDs are refused by the
  worker. Up to 32 distinct IDs may be nominated from one revision.
  Left mouse selection never generates an Inflate stroke in ON mode.
- **Backspace** clears selected FaceIds, **Enter** submits the
  explicitly nominated root and IDs to the FIFO worker, and toggling
  OFF clears the selection. A rendered new indexed revision clears
  old selections; expected-root checks independently refuse stale
  asynchronous input.
- Worker-side M47 checks *complete*, finite and source-certified
  transverse original contacts before invoking the existing exact
  Boolean. No all-geometry contact discovery occurs on the
  rendering thread. Valid operations create one COW root, dirty
  renderer owner snapshot and undoable history step. Unsupported
  source/tangent/ambiguous or over-budget input **refuses**; there
  is no guarantee that arbitrary user-selected shapes can be joined.
  Terminal post-CAS failure stops all subsequent operations.
  Accepted, refused and pending counts and fatal diagnostics are
  surfaced through title/HUD and console.

The three-compiler Actions build verifies full Vulkan compilation,
not an actual graphical interactive test. User-observed Linux/Windows
window placement, picking, source selection, and live Merge behavior
remain separate M52 / original M15 acceptance tasks. V18's
**Merge Off** collision constraints and V19's broader robustness
remain deferred.

## V17-M54 — Verified source-positive manifold union, separate fixed-floor rendering defect

The independent actual desktop `M→A→Enter` asymmetric/reflected
worker audit on all three CI platforms verifies original/accepted
immutable source/root history, correct two-opposite-face edge incidence,
zero boundary or nonmanifold edges, Euler V−E+F=2 for each component,
positive finite signed shell volume, zero accepted triangle
intersections/ambiguities, and zero CPU renderer-staged ghost,
missing or duplicate wires. Reflected: four original closed
components with 18 transverse triangle contacts become exactly
two closed, symmetric components (V32/E84/F56), each of
volume approximately 2.3434; undo/redo return those exact roots.
See `docs/V17_M54_ACCEPTED_UNION_AUDIT.md` and
[Actions #39471107991](https://github.com/logiclrd/Mesher/actions/runs/39471107991).

This establishes the finite named fixture Boolean as structurally
correct, NOT rendered correctly. Independently, the original
unit-sphere Z=−1.22 **depth-writing work floor** is drawn through
the Z=−3 tetrahedral fixtures. Its model-space placement and
a way to hide it for inspection are planned in **M57** before
M52 genuinely observed GPU acceptance. Do not mischaracterize
a validated manifold as a corrupt mesh simply because unrelated
grid geometry intersects it, nor claim all GPU line/depth behavior
is cured before real device testing.

## V17-M57 — Floor separation for source-positive tetrahedra

The previous Vulkan viewport always rendered a depth-writing XY work
floor at **Z=−1.22**, beneath the unit sphere but *through*
the supported M50/M51 tetrahedral fixtures whose original source
reaches Z=−3. This independent scenery-to-model intersection
could obscure valid union triangles and make the wireframe
appear to penetrate the volume. M54's fully verified actual
worker result (V32/E84/F56, two closed mirrored components,
zero post-union triangle crossings, zero ghost CPU wires)
shows the specific source-positive Boolean is structurally
sound.

The floor now has dedicated Vulkan solid/wire buffers,
distinct from both immutable COW partition geometry and
legacy mesh buffers, with an initial-source-bounds-aware
clearance: the regular sphere remains at floor Z=−1.22;
the two-tetra fixture floor is lowered to Z=−3.5.
Press **G** to toggle the actual floor AND its grid lines
without ever suppressing the model, and **W** independently
to hide/show lines. This does not change FaceIds, source
certificates, history, symmetry, picking or work limits.
See `docs/V17_M57_FLOOR_CLEARANCE.md`.

M52 still requires real human-observed Linux and Windows
Vulkan behavior; floor relocation is not a blanket proof
of correct GPU depth rasterization or freeform UI quality.

## V17-M58 — Smooth antialiased physical-pixel wireframe

Native Vulkan one-pixel `LINE_LIST` without multisampling
made the formerly depth-correct edge overlay extremely thin and
aliased on real Linux screenshots. M58 changes **only** the
visible wire render path. Each canonical two-endpoint model edge
(or grid segment) remains unchanged in the immutable CPU
partition renderer; at GPU upload only it expands to a
two-triangle screen-space quad. Vertex/fragment shaders use
the actual framebuffer size, a two-pixel halfwidth, one-pixel
coverage falloff and ordinary alpha blending so diagonals
remain perceptibly continuous. Solid depth is written first;
AA quads still read with LEQUAL and do not write depth.
Their clip Z is **not** pulled forward, retaining M56's
rear-edge occlusion. Near-camera and zero-projection segments
fail safely rather than streaking across the screen.

W still hides wire overlay; G independently hides M57
floor and grid. M54's positive reflected manifold union
and immutable source identity are unchanged. New
`screen_space_aa_wire_quad_encoding` and
`viewport_aa_wire_quad_pipeline_contract` CTests guard
GPU staging and source-level shader/depth state. The
formerly flaky asynchronous-test observer now waits for
both matching accepted root and history before asserting
(no worker production modifications).
See `docs/V17_M58_ANTIALIASED_WIRES.md`.
As with M55–M57, real Vulkan graphics appearance on
Linux/Windows remains M52 and original M15 stays open.


## V17-M62 — Lighter, smoothly antialiased model and grid lines

Following the user's physical Linux observation of continuous but
over-heavy M58 edges, M62 calibrates the existing depth-correct
screen-space overlay to a **1.5-pixel outer halfwidth** with
smoothstep antialiasing (a roughly 2-pixel effective stroke, down
from 3). If a nearer face masks half of a widened edge, only that
side remains visible; this **correct depth clipping** can
legitimately make a geometric silhouette look narrower, and is
not remedied by a global wire Z-offset. It remains important
to compare W-on/off and G floor visibility in real Vulkan
M52 observations. See `docs/V17_M62_WIRE_COVERAGE.md`.

## V17-M64–M69 — Stable AA wires through the 90° camera pole

The old M58/M62 detached screen-space edge quads could self-occlude
against the depth gradient of their owning triangles, abruptly doubling
visible line weight at exactly 90°. M64 documents and tests the cause.
Mesh edges now use M67's noperspective pixel barycentric distance in
**actual visible solid triangle fragments**; M68's independently spaced
integer XY floor grid is shaded within the true depth-tested floor
fragment, without a fake floor triangulation diagonal. M69 removes
obsolete GPU line buffers, uploads, and the old graphics pipeline.
The old reference shader/quads remain in source only as historical
CPU/analytic counterexamples and are not compiled by Vulkan.

**W** disables mesh/floor line shading; **G** independently hides the
whole floor. No global depth bias, hidden rear-edge drawing, backface
culling or source/worker edits were introduced. Real Linux/Windows
appearance—including the near/exact 90° transition—requires M52 user
verification rather than treating CI compilation as a screenshot.
