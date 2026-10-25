# R06-M20A1–A3 — Read-only local cap anomaly and candidate secondary hole

**CPU-tested implementation HEAD:** `5eac1ad5a2583d200128a54b3e0e3b4ead302e11`.
**GitHub Actions:** [#40225021188](https://github.com/logiclrd/Mesher/actions/runs/40225021188) **SUCCESS** on Linux GCC, Linux Clang, Windows MSVC `/W4 /WX`. **129/129 CTest tests passed on each**. The original five saved `.r06case` bytes were not edited; PR #162 remains OPEN/DRAFT and none of R06 publishes to worker root/checkpoints/history.

## What was requested

After M19A6 had reduced the pathological fold to a visible but nonpathological ripple, user suggested iterative recapping: diagnose abnormal CAP/CAP normal changes against the **original** survivor reference; excise the anomalous generated patch and a transition halo; re-cap that smaller hole; repeat only with bounded, measured improvement.

This milestone is the **first DIAGNOSTIC checkpoint only**. No original or cap face is deleted, no new hole is installed, no second cap is generated, and no source geometry or thread/history publication is authorized. The full design and future independently testable tasks live in [R06_M20_RECURSIVE_CAP_REPAIR.md](R06_M20_RECURSIVE_CAP_REPAIR.md).

## Implementation

- `include/mesher/repair_cap_anomaly.hpp`: pure `diagnoseSpatialCapAnomaly()`, using exact local face incidence, measured dihedral and centroid scale, a conservative lower-quartile original-survivor normal-variation background, and **locally anchored** confidence-qualified original M18 curvature where available. It reports source-crease cautions, low-confidence edges, a deterministic highest-unexplained-jump seed, bounded one-face halo, and a would-be contour from exact indexed oriented edges. Forks, duplicate/nonmanifold triangles, incomplete walks and finite-limit exhaustion refuse rather than fabricate a contour. It is *heuristic*, NOT a mathematically certified shape-operator continuation or a full source collision certificate.
- `SpatialCapPatch::reflectedOrbits`: carries the exact constructor-authoritative mirror mapping where applicable; no approximate geometric pairing or welding. Diagnostic proposed islands must have their mirrored cap faces.
- `src/repair_mixed_stage.cpp`: computes proposal on the final opt-in private cap before any cap-face staging, maps local indices to actual provisional/stable stage vertex IDs, records read-only face/contour metadata and publishes `DiagnoseCapAnomaly` as a **NO-CHANGE** step. Any unavailable diagnostic is labeled, and cap construction/strict downstream validator are unchanged.
- `MixedClosureStaging`, `RepairStepGeometry`, `buildRepairStepScene()`: retain bounded non-topological markers in immutable snapshots, including across multiple cap loops. Existing R03 yellow boundary rods stay authoritative; **orange = candidate anomalous cap faces** (material 4), **purple = proposed second rim** (material 5). Original step edit cyan/pink and ordinary cap green remain otherwise unchanged.
- CLI `--repair-recursive-diagnostic`: **default OFF** and legal only with interactive `--repair-step-debug --repair-replay FILE`. It can be combined with `--repair-feasibility-preview` to diagnose the M19A6 cap. No ordinary automatic sculpt or headless replay test mode can activate it.
- HUD: `M20A EXCISION PROPOSAL ONLY - NO FACES REMOVED; NO PUBLICATION`, proposal counts and source-confidence cautions. The diagnostic may add one no-change debugger operation before the first cap `EnsureFace`, so find operations by their names rather than assuming step numbers remain fixed.

## Exact archived test result

On immutable `repair-cases/refusal-1.r06case`, with M17C/D, M18D/E and M19A6 selected positional preview:

```
[r06-m20a-refusal-1] suspect_faces=6 proposed_loops=1 source_rim_touch=0 private_only=1
```

This passes on all three CI platforms. The original candidate `SpatialCapPatch` face list and positions are unchanged; R03 rim geometry is untouched. Other synthetic tests cover healthy planar cap no-op, folded generated cap, ordinary/reflected source topology, intentional source normal crease, deterministic replay, duplicate indexed faces and finite work refusal. The step-scene CPU regression verifies orange/purple render materials, face/rod counts, and no stage topology mutation.

This **does not** prove the six faces match exactly the visually undesired ripple, that the secondary rim normals are healthy, that one new cap will be better, or that any recapped candidate can pass complete source-backed manifold/embedding validation. Those are respectively USER GPU visual acceptance, M20B, M20C and M20D.

## Manual user Vulkan inspection

Rebuild the exact accepted branch and run:

```bash
build/Mesher \
  --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson \
  --repair-feasibility-preview --repair-recursive-diagnostic \
  --repair-seam-audit --repair-overlay-exact
```

Look for `LATEST OP: DiagnoseCapAnomaly` before new cap faces, followed by the cap `EnsureFace` operations. Advance until the orange faces are staged, then orbit around the purple secondary contour and the source rim. Compare with `--repair-recursive-diagnostic` removed. Note: the initial no-change marker step can shift the ordinal around historic 53/54. The proposed rim is not the live yellow open boundary.

## Next steps / acceptance

- [ ] User independently confirms proposed orange 6-face region and purple one-loop contour actually bound the visible ripple, including the mirrored side and downstream private attempt verdict.
- [ ] M20B: actual local PRIVATE excision on a copy, followed by an open hole preview and exact source/rim/quality checks.
- [ ] M20C: attempt ONE cap replacement constrained by the original survivor reference and original indexed rim; accept only if entire candidate improves and certifies.
- [ ] M20D: finite iterative recursion, healthy/crease/torus/hollow fixtures and GPU acceptance. No published sculpt repair without subsequent R07/R08 work.
