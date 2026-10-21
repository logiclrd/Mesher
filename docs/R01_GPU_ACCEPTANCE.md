# R01 physical acceptance — first post-stamp intersection freeze

**Status:** Implementation/CPU validation in progress. Linux and Windows GPU inspection not yet accepted. This is an explicitly opted-in **developer diagnostic**, NOT the production approximate repair engine.

## Expected behavior

`--repair-inspect` is exclusive with `--merge-geometry`, `--avoid-collisions`, demo and compare modes. Regular startup and protected Merge/V18 OFF constraint remain unchanged. The first newly evidenced triangle intersection after bounded post-Inflate/Refine/Collapse detection freezes the brush: no subsequent stamp, undo/redo or checkpoint is allowed until exit. Scene orbit, pan/dolly, W/G and Escape remain available. The last proposed root is displayed **read-only** (not published, not saved); red octahedral dots mark all original vertices INCIDENT TO contacting faces, and darker red rods mark their attached original edges. The mathematical intersection might lie entirely inside triangles, so these dots are NOT supposed to be exact crossing locations. Ambiguous triangle pairs appear in diagnostics and deliberately trigger freeze. Incomplete work-budget detection still freezes, but is explicitly identified as `QUERY_INCOMPLETE` (with no red markers if no contacts were certified); it must never be mistaken for a detected intersection. The inspection budget for persistent BVH directory box visits is 4,194,304: each changed face initiates its own traversal, and the earlier 131,072 limit was exceeded by ordinary default symmetric sphere sculpting (898 seed faces, 5,935 narrow-phase tests, no contact evidence). The window title distinguishes `R01 QUERY INCOMPLETE (FROZEN)` from `R01 CONTACT (FROZEN CANDIDATE)`. Collision detection is triangle-based, **not** point winding. No topology deletion occurs.

## Reproduce on Linux

From a build of the exact PR SHA:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher --repair-inspect --asymmetric --avoid-test-sheets \
  --no-refine --no-collapse --no-curvature-correction \
  --strength 0.9 --brush-radius 0.5 2>&1 | tee r01-linux.log
```
Orbit until the close overlapping triangle pair at approximately X=0 is visible at a strongly oblique angle. The third isolated triangle at X≈3.5 is a control. Before triggering, sculpting **that remote face** must not freeze the application. To trigger, put the brush center **near a vertex** of either of the two close, 2-unit-edge triangles; at brush radius 0.5, a click at the middle of a large triangle may influence *no* vertex and hence cannot be a positive test. One inward stamp at strength 0.9 should produce a transverse candidate. Look for `[repair-r01] FROZEN candidate contacts=...`, individual FaceId pairs and `R01 FROZEN (UNPUBLISHED CANDIDATE)` in the window title. Confirm visible red dots and dark-red edges on both participating faces from both outside and interior viewpoints. Holding LMB or clicking again must not change mesh geometry; RMB orbit, Ctrl+LMB pan, Ctrl+RMB dolly, W/G and Escape remain usable. Relaunch resets freeze.

If the fixture fails to freeze, record the actual brush radius, cursor location, any `influenced_vertices` diagnostic, camera view and logs. **Do not count a clean stamp as positive collision evidence.**

## Windows PowerShell

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
& .\build\Release\Mesher.exe --repair-inspect --asymmetric --avoid-test-sheets `
  --no-refine --no-collapse --no-curvature-correction `
  --strength 0.9 --brush-radius 0.5 2>&1 |
  Tee-Object -FilePath .\r01-windows.log
```

## Observed Linux manual exercise (2026-10-21; partial physical evidence)

A user-provided physical viewport screenshot captures a default symmetric sphere sculpted into two long opposed protrusions that were intentionally brought into contact. R01 appears to freeze at the intended first collision. Red octahedral markers and attached edge rods are visible on both sides from a camera placed near the surface boundary; the highlighted topology includes a one-triangle incident-face halo beyond the one visibly penetrating vertex. That *is expected R01 visualization* and is not a claim that every marked vertex penetrates or should ultimately be excised. This positive real-world example contrasts with two earlier **failed** sphere exercises: spurious convex-hull shared-vertex contacts (fixed by endpoint snapping) and diagnostic broad-phase budget exhaustion with no evidence (addressed by a larger finite budget and differentiated messages).

This is meaningful **partial Linux GPU observation**, not full R01 acceptance: no corresponding verified Windows GPU screenshot/log, controlled fixture/negative exercise and exact binary provenance were supplied. R01 should remain unchecked. A future positive review of R02/R03 must distinguish highlighting all vertices incident to intersecting faces from an actual proposed *deletion* set, since blindly removing their complete vertex fans could over-excise.

## Additional acceptance and evidence

Repeat on an ordinary sphere with `--repair-inspect` (without the test fixture); ordinary stamps not involving intersections should continue to work normally. Record both hosts' exact `git rev-parse HEAD`, binary SHA256, OS/GPU/driver and clear screenshots of frozen geometry and red markers, plus logs. Confirm default `Mesher` doesn't freeze or display markers and `Mesher --repair-inspect --avoid-collisions` fails CLI preflight. CPU regression `repair_r01_frozen_intersection_inspection` must pass alongside all existing suites. Distinguish **CPU/CI integration** from actual physical hardware acceptance; leave R01 TODO unchecked pending both host observations and merged-main CI.

R01 doesn't infer buried-face classification, group repair islands, cut faces or select caps/zippers. Those are R02–R06. R01 compares each candidate FaceId pair against contacts in the previous root and does not automatically freeze on pairs already intersecting before the stamp. Actual worsening and topology-changing identity matching remain R07 work.
