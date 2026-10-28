# R06-M17A — Bounded split-template planner: CPU acceptance

**Date:** October 24, 2026. **Code head:** `f0cf3798fee405e70b8dde9b559952b28ceba74b` on existing `r06-integration` (draft PR #162).

**Three-platform acceptance:** [GitHub Actions #40126355956](https://github.com/logiclrd/Mesher/actions/runs/40126355956), attempt 2, **success** on Linux GCC, Linux Clang, and Windows MSVC `/W4 /WX`. All **109 Windows CPU tests** executed. The first attempt passed the NEW M17A test but had one unrelated intermittent M39 `asynchronous_sculpting` history/redo failure. Re-running only failed Windows jobs against the SAME exact source commit passed. The M39 nondeterminism is not fixed by this milestone.

## Accepted read-only functionality

`include/mesher/repair_split_template.hpp` defines `evaluateRepairSplitTemplates(RepairSplitTemplateInput)`, returning a bounded enumeration of admissible/rejected proposals and a conservative suggested template. The planner never edits mesh topology, private COW revisions, worker publication, or any existing repair threshold.

- Canonical stable-endpoint ordering and projection `t=dot(C-A,B-A)/|AB|^2`; candidate midpoint, single off-center foot/blend, and midpoint followed by at most one secondary foot/blend split inside one child interval.
- Accepts at most two incident third vertices with separate child-quality floors, scoring all resulting children of BOTH faces. Reports worst original/child normalized quality, shortest projected base interval, longest remaining interval and explicit refusal descriptions; validates finite geometry, target length, fractions and budgets.
- Honors a provisional coarse band `length(AB) / targetLength > 2`, finite operation and depth budgets, hard minimum split-segment fraction, and positive under-quality gain in the coarse band. A face below quality floor INSIDE the shape-repair band does not get blindly subdivided again; requests shape correction.
- A normal large healthy face prefers a single balanced midpoint, while off-center fallback is only proposed if its complete two-sided assessment is safe. A two-operation hybrid is a PROPOSAL, not an automatic cure or unbounded recursion.
- **Important mathematical counterexample:** for the skinny `A=(0,0), B=(10,0), C=(8.2,0.4)` triangle, the midpoint split makes the worst child q WORSE, and splitting the other child at the perpendicular foot afterward leaves that original worst child unchanged. Both are explicitly REJECTED in the test. The projected foot can be an admissible *single* split in this read-only assessment, but real reflection/remote-source collisions and topology remain unexamined until M17B.
- The near-endpoint foot `t=0.98` is rejected for both the direct and two-level hybrid patterns, preventing a perpendicular-foot zigzag cascade. A hard two-step cap and finite candidate list preclude indefinite local subdivision.
- Reversed caller endpoints normalize to identical stable-ID canonical `t`, and reflected physical coordinates preserve corresponding canonical candidate locations.
- No proposal is selected for healthy, already-small edges, degenerate geometry, exhausted budgets, or an under-quality face in the shape-repair band.

**Independent regression:** `repair_r06_m17a_bounded_hybrid_split_template_policy` (in `tests/repair_split_template_tests.cpp`), included in the existing CPU-only and Vulkan-enabled CI configurations; positive/negative coarse size, bad sliver, hybrid, two-sided different projection, reversed endpoints, reflected coordinates, and strict budget cases. None of the original five `.r06case` payloads changed.

## Remaining independent work

**M17B** is NEXT: make the actual `MixedClosureStaging::splitEdge` transaction accept a canonical parameter and synchronize it over the complete current two-sided and mirrored orbit, with full explicit original-survivor child checks, remote XYZ identity collision checks, and M16 debug exact `t`/XYZ/lineage. Keep midpoint signature and default behavior unchanged.

**M17C** would consume the planner in the real mixed-closure refinement loop; the result may legitimately choose shape repair/refusal instead of any split. It also needs an explicit objective for evaluating the hybrid's longer-term trajectory, rather than pretending that its second split necessarily improves the unchanged first child.

**M17D** addresses the actual upstream folded-back spatial-cap front around `refusal-1` step 53 and tests mirrored diagonals/ring positions before staging; **M17E** remains true Linux/Windows GPU and full-case acceptance. M16D/F physical tests, R04/R05 shared-executor migration, strict q and publication boundaries remain separately open.
