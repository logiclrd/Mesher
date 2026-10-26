# R06-M22 — Generic Poisson core before in-place advancing-front replacement (2026-10-26)

## Correction to initial plan

The original M22A mirrored cross-seam bridge kernel was premature and
unnecessary as a standalone implementation. The R06 input contour already
requires every original edge to stay in a closed half-space and intersect
X=0 only at an **existing seam vertex**. The advancing-front constructor
must preserve that invariant at every new front operation.

For a selected *off-seam* vertex whose incident front edges respect
the invariant, both neighboring vertices lie on its own side of X=0
or on the seam; the reflected counterpart can be advanced atomically
using the ordinary rule. There can be no off-seam selected vertex with
an edge to its own reflected counterpart.

Caveat: a naïve advancing-front operation *at a seam vertex* could connect
a left neighbor to a right neighbor by a new illegal cross-plane edge.
Thus seam vertices need an explicit **admissibility** rule: forbid actions
that would bridge opposite half-spaces; stage symmetric half-disk work
together and maintain true X=0 seam-edge chains. This is symmetry-aware
choice/ledger management, not a separate three-case crossing-edge handler.

Do NOT resurrect `repair_cap_advancing_front.hpp` seam-bridge-only code.

## Architectural separation

The old `buildSpatialCapFrontPoisson` creates an exact n-to-n inset
ring followed by a hub; its graph-Poisson solve assumed generated vertices
were exactly indexes n..2n and hub 2n. This mixes construction topology
with coordinate optimization. First introduce a GENERIC fixed-rim graph
Poisson core that accepts:

- `SpatialCapPatch` with `boundaryCount` fixed rim positions,
  arbitrary additional vertex count and indexed triangular faces;
- source-backed guidance directions indexed to arbitrary interior vertices,
  not derived from `vertex - boundaryCount` as an original rim ordinal;
- explicit, involutive exact mirror-orbit mapping when symmetric;
- original iteration / residual / budget / finite / source-rim checks.

Preserve *bit-compatible behavior* for existing ring/hub call by supplying
the original n ring-normal directions plus the averaged hub normal. Test
non-ring planar/warped patches as well as the old fixed-shape fixtures.
The old constructor can call the generic solver without changing the R06
closure path. `buildSpatialCapFrontPoisson` retains its public entry point.

M17–M21 position/diagonal refinements currently assume the n-ring/hub
layout. They cannot run on a variable-topology advancing-front patch
until adapted to generic local connectivity; give the new candidate
explicit compatibility guards or rewrites before enabling previews.

## Later AFM in the SAME cap construction pipeline

After the generic solver has an independent test suite, replace the
ring/hub geometry producer IN PLACE using Zhao, Gao and Lin (2007)
§4 advancing front with legal ordinary corner operations, signed
interior angles and deterministic tie breaking. Every logical front
edit and its reflected counterpart are staged atomically. Maintain
the original R03 rim IDs/positions/winding without approximate welds.
The PAPER'S Poisson reconstruction from rotated desired-normal triangle
gradients (§5) is a distinct later comparison, not part of topology
replacement.

Mirror invariants: exact X=0 seam vertices, no cross-plane edge,
no off-plane face across X=0, explicit paired faces with opposite
winding, connected disk/half-disk front. At a seam-selected vertex,
reject/refine an operation if its new edge would straddle X=0.
Avoid a topology dead end by testing legal seam-front progression;
never silently allow crossing triangles.

Every intermediate/private candidate must meet finite-work and
source-backed tests. The final capped source index must have
original rim identity, q>=.10 for cap/zipper, existing survivor child
q>=.012 except certified rim-tip exceptions, legal orientation, no
new T-junction, forbidden contact, correct vertex fans and exact mirror
orbits. Physical Vulkan judgment on refusal-1 remains separate
from numerical/CI evidence.

The old ring/hub construction stays available temporarily for
matched-view A/B and **validated full-closure fallback**: a speculative
alternative must not turn a successful original cap into failure.
Neither local patch validation nor CI alone authorizes publication.

## Flat plan (short, independently testable stages)

M22A — Extract generic fixed-rim Poisson solve from existing
`buildSpatialCapFrontPoisson`, preserving the current path; test
variable topology including mirror maps and refusal/finite budgets.

M22B — Replace ring/hub initialization with advancing front in the
SAME entry point; exact source-rim and X=0 seam constraints; first
PRIVATE A/B, no M17–M21 invalid ring-specific assumptions.

M22C — Source-backed end-to-end validation, completed retry/fallback
when optional AFM fails, no worker-root publication.

M22D — Separate rotated-desirable-normal/gradient Poisson candidate,
compare same triangulations against existing graph-Poisson.

M22E — GCC/Clang/Windows MSVC CI, archived-case and physical matched-view
A/B, performance, acceptance and only then a production-default decision.
