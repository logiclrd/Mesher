# R06-M17C — Scale-aware closure refinement: CPU acceptance and step-92 finding

**Accepted 2026-10-24 (CPU, NOT physical GPU):** exact code commit `a0dd13ee888eb0d16bec90770ad5fb0c15e196a0` on `r06-integration`, existing draft PR #162. [Full GitHub Actions CI run #40142700543](https://github.com/logiclrd/Mesher/actions/runs/40142700543), attempt 1: SUCCESS on Linux GCC, Linux Clang and Windows MSVC `/W4 /WX`, including all 112 CPU tests. Do not claim physical Vulkan correctness or publish an untrusted R06 private result.

## Implemented, explicitly opt-in

`--repair-scale-aware` is accepted with live `--repair auto`, `--repair-replay FILE`, `--repair-replay-test FILE` and interactive `--repair-step-debug --repair-replay FILE`. It is **off by default**. Disabled saved-case replay retains original midpoint geometry, strict quality/reflection/validity policies and publication boundaries.

- M17A's finite read-only planner evaluates midpoint, off-center and midpoint-plus-one-corrective hybrid proposals with the complete two-sided incident triangle qualities, bounded local work/depth, minimum interval fraction, stable canonical split t and mirrored geometry requirements.
- M17B's single staging-owned conforming editor applies explicit canonical t to actual source-backed and generated incident faces atomically across the full mirror orbit; recomputes the one corrective subedge proposal after the midpoint before ever staging a second split. No unbounded perpendicular zigzag chasing.
- A local face with q below the existing hard floor enters shape-first evaluation when its longest edge is <= 2 times local target, using finite new-cap interior diagonal flips, existing three-triangle cap-fan retirements and bounded new-interior-vertex fairing. No general cap/survivor seam collapse or flip has been introduced.
- **Important M17C correction after physical operator observation:** if all shape edits decline, do not *categorically* refuse every subdivision at shape scale. A ONE-operation split can be allowed if it demonstrably makes *every* resulting face on BOTH incident sides reach its preexisting hard q floor. The counterexample shallow triangle A=(0,0), B=(10,0), C=(5,0.4) starts under q=.1, while the midpoint children exceed it; a second skinny neighbor blocks the same split. The genuinely folded acute needle remains uncorrectable via this mechanism.
- Every unsuccessful shape attempt is now a distinct diagnostic-only `MixedClosureStaging::reportRefinementAttempt` event for the M16 single-step debugger. The screen can show `TryDiagonalFlip`, `TryCapFanCollapse`, `TryInteriorFairing`, and `TryQualityRestoringSplit`, each with the selected face/quality/eligibility, `NO_ADMISSIBLE` or refusal rationale and unchanged exact staged geometry. These report attempts, NOT successful repairs. All existing HUD positioning, previous/current frame and cancellation contracts remain intact.
- Strict failure retains explicit reason, never publishes a partially closed model or weakens q, mirrored topology, geometric collision or closure proof. The existing planar fixed-rim quality certificate remains independent and valid.
- Baseline full original five `.r06case` fixtures remain unchanged; no manufactured success or revised expected-outcome bit.

## Concrete operator regression: refusal-1

User observed `--repair-scale-aware --repair-replay refusal-1.r06case --repair-step-debug` stop immediately after step 92 `ResolveProvisionalVertices` with:

`R06-M17C UNDER-QUALITY AT SHAPE SCALE NEEDS A LEGAL FLIP/COLLAPSE/REPOSITION, NOT ANOTHER SPLIT: FACE=3207,5470,6189 Q=0.061107`

Root of UI discrepancy: the code DID attempt the three shape operations but the step observer only recorded successful mutations. The available operations are deliberately narrow (same-kind new nearly coplanar face pair, new cap center valence 3, new movable unprotected vertex). None found admissible geometry. The previous scale-first policy refused without considering a rare fully quality-restorative short split.

**Exact original 531KB archived file**, not a recreated stroke: `repair-cases/refusal-1.r06case`, passed through original R02, R03, M03 and opt-in M04 private closure with diagnostic operation observer. CPU output:

```
[r06-m17c] exact refusal-1 saved case: 4 no-change repair diagnostics, 0 staged split vertices, strict refusal PASS
```

Thus the CURRENT update makes the missing attempts visible and proves no split was committed, **but refusal-1 still refuses**. The synthetic q-restoring case is permitted; the original folded-back initial cap is NOT automatically fixed. R06-M17D must target the spatial generator's initial ring placement, mirrored quad diagonal selection, fold-back/dihedral and q admission BEFORE new cap triangles are staged; do not spend hundreds of subdivisions reproducing the same needle.

The real archived source is already an indexed checkpoint with legitimate tombstones. The test compares the **initial** retired-face count to the **final** count, rather than incorrectly assuming no existing retired IDs. Earlier CI caught that incorrect test assumption; corrected exact-head three-platform acceptance is linked above.

## Next gates

- R06-M17D: evaluate fold-free admissible initial spatial-cap candidate constructions, with preflight mirroring and fail-closed geometry checks; specifically inspect step-53 face and the unchanged original refusal-1 data.
- R06-M17E: physical Linux and Windows Vulkan before/after replay, step visibility and overlay accuracy, manifold and no-hidden-cavity validation; compare ordinary versus scale-aware modes.
- M16/forced R04/R05 shared executor migration remain separate work. The M17C acceptance means the **opt-in control flow** is CPU correct; it is NOT a resolution of all real topology defects.
