# V17-M50 — Explicit source-positive asymmetric desktop Merge fixture

**Scope:** first independently finishable peer in the October 20 flat
original-M15 plan. Existing M47/M48/M49 are complete. **Original M15,
M51/M52, V18 and V19 remain open.** This is **not** a rendered GPU test
or a claim of an imported/universal Boolean.

## Why a source fixture is necessary

Before M50 the real `Mesher --merge-geometry` executable always started
with a newly generated closed sphere. M47/M49's genuinely positive live
Merge worker tests instead constructed two already-intersecting, independent
closed tetrahedral source sheets in a test-only function. Starting the real
GPU executable therefore did not provide a reproducible known-positive
original-source example, even though the underlying worker was proven.

M50 exposes **only** that exact supported fixture to the ordinary
desktop runtime. Run:

```bash
Mesher --merge-geometry --asymmetric --merge-test-tetra
```

All three options are deliberate: original-source Merge authorization,
asymmetric vertex ownership, and the named developer test fixture.
`--merge-test-tetra` without `--merge-geometry`, or with symmetric
mode, is rejected **before** window/Vulkan startup. Other incompatible
demo/recovery/comparison/noninteractive modes keep M48's refusals.

The fixture in `include/mesher/desktop_merge_fixture.hpp` uses eight
permanent source vertices and eight correctly wound faces; each four-face
tetrahedron owns a different set of vertices, and their complete original
contact arrangement is the supported M47/M49 asymmetric positive.
The exact same fixture is now returned by
`tests/async_sculpt_tests.cpp::twoIntersectingTetrahedra(false)`, so
tests cannot silently drift from the user-launchable geometry.

The **same production `AsyncSculptWorker` and GLFW/Vulkan renderer**
receive it as an untouched fresh first original, with protected
`physicalExact` source authority provisioned only through
`--merge-geometry`. Mode remains **OFF** at launch. No original faces
are auto-selected and the fixture alone schedules **zero** Merge worker
commands. To request surgery, the user must turn ON with M and explicitly
select source FaceIds and Enter. M47 retains its mandatory independent
whole-source finite original contact discovery, M21/M29 source and intent,
38 operation/resource bounds, terminal d2 geometry validation, one
original-root CAS/history and M14 post-CAS quarantine policy.

## CPU / CI acceptance gates

- The M47 and M49 actual worker/controller regressions now consume the
  **same asymmetric fixture factory** used by `src/main.cpp`.
- New `testM50DesktopAsymmetricFixtureAndProtectedWorker` asserts
  exact fixture cardinality/topology, zero latent queued or published
  changes at startup and **one** explicit complete-worker positive with
  original-root picker pointer retention, one source history transition,
  final dirty immutable snapshot and actual accepted live request.
- Two new **real executable** GPU-free CTests reject fixture access from
  unarmed and symmetric startup; the existing four M49 executable
  gates remain unchanged. With the viewport enabled there are now
  **29** registered CTests (23 CPU + 6 executable CLI).
- Full final PR-head GCC/Clang/MSVC warning-clean Vulkan-enabled builds,
  existing checkpoint/history stress and the independently merged-main
  suite are required before checking M50. Do not mark it complete from
  source review alone.

## Explicit residual / no false M15 acceptance

An initial fixed camera view may not expose every first-original face.
The M47/M49 positive controller tests nominate **all eight** original
FaceIds, whereas arbitrary on-screen clicks only see exposed faces.
M50 does not secretly expand a picked face to an unselected source
patch; an incomplete deliberate scope may rightfully refuse.

M51 owns a separately authenticated reciprocal reflected fixture and a
**deliberate** bounded whole-original nomination UI for otherwise
occluded faces. Selection must remain an explicit user request, bound
to one displayed immutable root and capped at 32; its implementation
may not infer equivalence from coordinate coincidence or do a
V13 whole-mesh collision scan on the rendering thread.

M52 requires actual **observed** Linux and Windows GLFW/Vulkan input,
HUD, source-positive final draw/picking/dirty owners, rejection, symmetry,
undo/redo and checkpoint behavior on graphics hosts. A passing CLI test
does not constitute any such observation. Original M15 stays unchecked
until all combined requirements are fulfilled. V18/V19 remain separate.
