# R06-M19A3 — Provisional stepwise preview acceptance

**CPU accepted code SHA:** `f88103018aa90814c7889e74a3014e15b97021f5`
**CI:** [#40207792552](https://github.com/logiclrd/Mesher/actions/runs/40207792552)
SUCCESS on Linux GCC, Linux Clang and Windows MSVC `/W4 /WX`, **124/124 CPU tests on each**.
The later documentation-only commits are not part of that tested code revision.

## Motivation

M19A and M19A2 were *pure diagnostics*: they found locally admissible
new first-ring positions, but the stepped Vulkan debugger still drew
the original M18E cap. The user correctly identified that the
screenshots couldn't distinguish these candidates. M19A3 adds an
**explicitly provisional**, private-only stage option to display the
actual best M19A result using the existing R06 step debugger's
geometry-rendering path.

## Invocation

```bash
build/Mesher \
  --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug \
  --repair-scale-aware \
  --repair-front-optimize \
  --repair-curvature-optimize \
  --repair-curvature-poisson \
  --repair-feasibility-preview \
  --repair-seam-audit --repair-overlay-exact
```

Remove **only** `--repair-feasibility-preview` for the
identical M18E control. The existing `--repair-feasibility-probe`
retains its strict read-only behavior and logs normal-sweep metrics.
The new flag is disallowed without **interactive**
`--repair-step-debug --repair-replay FILE`, including ordinary live
repair and headless replay test mode. The debugger HUD always includes:

`M19A PROVISIONAL POSITION PREVIEW - NOT CERTIFIED; NO PUBLICATION`

The builder also emits
`[repair-r06-m19a-provisional-stage] candidate_applied=1`
or `candidate_applied=0` (if the finite search finds no suitable
candidate), plus the local minimum triangle quality.

## Implementation boundary

The bounded M19A helper now accepts an optional output pointer for
the best locally admissible `SpatialCapPatch`; when absent it
remains strictly read-only. The existing M19A search uses identical
fixed R03 contour coordinates, immutable face connectivity, mirror
orbits and the original cap-quality, winding, local cap self-contact
and cumulative work-budget checks.

Only when the explicit **step-debug-only** flag is set does
`buildSpatialCapFrontPoisson` replace its local cap result with the
counterfactual first-ring positions before
`MixedClosureStaging::stageProvisionalVertex` assigns PRIVATE
synthetic vertex IDs. The normal staged `ensureFace` calls then
generate green/cyan faces at the proposed locations, meaning
step 53/54 and subsequent operations can display those positions.
A candidate that has no local improvement is left unchanged.

No alternate face connectivity, immutable R03 rim, source
`VertexId` or `FaceId`, published worker root, history or checkpoint
is changed. The complete original R06 private auto-closure still
performs its unmodified global mesh/incidence/collision/reflection
certification and may **refuse** after these step frames are shown.
Cap-local acceptance is NOT global repair certification. No physical
Vulkan acceptance is claimed.

## CPU regression evidence

On the exact original immutable `refusal-1` case with the M17C,
M17D, M18D and M18E controls, the `repair_scale_aware_refinement_tests`
directly compares legacy M19A read-only and provisional-stage
construction:

- Existing read-only probe yields bit-identical cap positions,
  faces and quality to the selected M18E baseline.
- Provisional mode stages **five changed inward-ring vertices**.
- Original R03 contour positions, face indices and cap topology
  remain identical.
- Minimum cap triangle quality remains **0.11750271253518528**
  (above the unmodified `q>=0.10` cap floor).
- The worst seam-normal discontinuity is not permitted to worsen.
- Dedicated CLI contract tests reject the provisional switch
  without interactive stepped saved-case replay.

The test directly verifies the private cap builder's output, and the
step debugger renders the same builder's staged provisional vertices.
Real Linux/Windows Vulkan inspection and the full cross-survivor
evaluation of the *modified* proposal remain open.

## Next tasks

Inspect the geometry around stepped `EnsureFace` #53–54 under the
new opt-in, compare with the baseline from the same view, and obtain
the user's visual diagnosis. Do not claim that the new geometry
eliminates the 52.97-degree worst seam angle: M19A2's finite
normal-sweep diagnostic did NOT improve that peak seam. Later M19D
can still trial the substantially more promising local diagonal
counterfactual under complete whole-mesh certification.
