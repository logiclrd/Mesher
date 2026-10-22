# R03 — Frozen private vertex/face excision: inspect actual open holes

**Status:** Independent implementation PR, stacked on R02 (#137), which is stacked on R01 (#136). Automated checks and physical Linux/Windows Vulkan acceptance are independent. This diagnostic must **never be merged as a production live sculpt mode** or counted as full repaired geometry.

## Command line

```sh
git fetch origin pull/138/head   # use the actual R03 PR number after creation
git switch --detach FETCH_HEAD
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher --repair-excise
./build/Mesher --repair-excise --torus
./build/Mesher --repair-excise --hollow-sphere
```

`--repair-inspect` remains the R01 red-contact diagnostic; `--repair-preview` remains R02's orange speculative selection. `--repair-excise` uses precisely the same R01 contact detection and the same R02 **complete vertex-star** proposal, but on the first completed contact query it actually deletes every proposed stable VertexId and FaceId **inside a separate private copy-on-write mesh revision**. The committed worker root, immutable post-sculpt candidate, active history, checkpoints, symmetry state, and saved document remain unchanged. The brush is permanently frozen for that run; LMB/undo/save remain disabled, while orbit, zoom, W/G and Escape continue functioning.

The private mesh is an intentionally **open orientable manifold with boundary**, not a closed two-manifold. This intermediate state should have boundary edges incident to one triangle, with a single connected link fan per surviving vertex. It is not automatically a non-manifold surface merely because it has holes. The normal `RegionEdit::validate()` requirement for two oppositely oriented faces at each edited edge is **left untouched**; R03 uses a debug-only lower-level COW view followed by an independent read-only index build, not a softened production `RegionEdit` path.

The private snapshot carries the exact previous stable IDs of all survivors, adds tombstones for deleted IDs, preserves previous tombstones, and recomputes read-only picking/connectivity indices. There is no cap, zipper, reconstruction, remeshing, or hidden material classification in this mode. The viewport renders **actual missing triangles and clean holes**. **Only gold surviving-rim rods** are shown on a successful cut; R02 orange/red ghost triangles must not obscure the gaps. A fully removed closed component is allowed to have zero gold loops because it leaves no open boundary.

R03 audits all faces of the private cut under a separate finite one-time diagnostic budget: no dangling triangle references, every surviving edge has one or two incident faces, two-faced edges have opposed winding, and the set of one-sided edges **exactly equals** R02's directed predicted contours. Each surviving affected vertex must also have one connected topological fan: a closed cycle in the interior, or one open chain with two endpoints on a hole rim. Missing, extra, duplicate, or badly oriented edges refuse the private cut. Every deleted ID must become a tombstone. If the R02 proposal is incomplete, ambiguous, exceeds finite limits, or does not pass this audit, R03 **does not substitute a partial cut**: it freezes the uncut candidate and prints `[repair-r03] EXCISION_REFUSED ...`.

Successful diagnostic:
```text
[repair-r03] EXCISED private_open_mesh=1 removed_vertices=... removed_faces=...
  modified_partitions=... surviving_faces=... open_boundary_edges=...
  open_boundary_loops=... no_caps=1 no_zippers=1 unpublished=1
```

## Physical acceptance scenarios

1. **Default sphere — colliding protrusions:** create two opposing probosci, verify first contact freezes, and inspect the *real* deleted opening in both directions. No red/orange geometry should remain across the gap. The two boundary loops should match R02 gold loops and have no branching or orphan points.
2. **Torus 0.55/0.45 — central pinch:** inflate the inner sides into contact. Inspect the open holes from inside the torus, with wireframe on and off, and compare against R02 prediction. This is not yet genus-changing closure, so neither pair of holes should have been capped.
3. **Hollow sphere — mouth pinch:** look through the thin gap between inner and outer shells; verify the pinch region now has genuine deleted faces. The remaining inner shell is deliberately still present (not yet a disconnected redundant component). Redundant-shell elimination is a later milestone after a successful closure.

For each run, capture the exact SHA, complete `[repair-r02] PREVIEW` and `[repair-r03] EXCISED` or `EXCISION_REFUSED` diagnostics, plus inside/outside screenshots; verify orbit/zoom/W/G and LMB/undo/save refusal. Windows physical GPU acceptance is a separate gate from GCC/Clang/MSVC test results.

## CPU test contract

`repair_r03_private_open_boundary_excision` exercises a genuine private indexed cut, not a fake renderer mask: dual sphere holes, synthetic finite contact selection near the tight torus and hollow-shell mouth, a whole-component deletion with **zero loops**, complete face-star preflight and incomplete-plan refusal, exact survivor contour versus observed one-sided edges, preserved stable IDs/tombstones, COW renderer invalidation, and an actual post-remesh R01 contact freeze with unchanged worker/history. Deliberate unsupported many-to-many island matching belongs to later repair stages.

**Exit criterion:** One clear open-surface screenshot each from sphere, torus, and hollow mouth, with diagnostics confirming *actual* private deletion and no stray triangles or orphan vertices. R03 remains unchecked until those physical tests pass.
