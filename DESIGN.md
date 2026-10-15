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

The scripted `--demo` and CPU `updateTimed` / `finishTimed` methods
retain **exact 4D Step sampling**, including all intermediate samples.
Interactive mouse painting instead uses `updateTimedRealtime` and
`finishTimedRealtime`, which enforce a **maximum of one actual brush
stamp per input/render iteration**. This prevents a long or fast mouse
movement from generating an unbounded number of expensive synchronous
mesh modifications before the next Vulkan draw.

When a mouse update contains more than one due 4D Step, the live sampler
uses the **latest grid sample**, skips earlier samples and advances the
sampling phase over the entire trajectory. Subsequent input begins from
the latest mouse position/time, never from the old missed work. Release
uses at most one final stamp at the exact current pointer location,
without applying a backlog. The window title reports the cumulative
`skipped` stamp count in addition to actual stamps. During normal
movement and when geometry processing keeps pace, no stamps are skipped.
The HUD's actual STAMPS rate now directly measures geometry changes
that can be presented between frames, rather than arbitrary batches
completed within a single stalled frame.

This is a deliberate **real-time responsiveness versus full path fidelity**
trade-off. If input motion demands more samples than the CPU/GPU can
process and display, the real-time mode reduces the sampling density
rather than allowing latency to grow without bound. It preserves
the four-dimensional arc-length *phase*, but not every intermediate
stamp under overload. The offline mode remains available to exercise
the complete stroke with zero skipped stamps; longer term, spatial
acceleration and efficient local remeshing should reduce the need to
drop samples. The operation does not assume a particular display
refresh rate.
