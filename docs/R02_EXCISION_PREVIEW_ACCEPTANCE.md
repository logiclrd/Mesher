# R02 — private vertex-star excision and oriented rim preview

**Status:** Revised following three failed/partial physical acceptance exercises (normal sphere, torus and hollow sphere). Code is on stacked [PR #137](https://github.com/logiclrd/Mesher/pull/137), based on open R01 #136. GPU acceptance and final merge remain OPEN. No topology is edited or published by this preview.

## Why the original R02 failed

The original R02 selected *whole faces* when V14 barycentric sample voting marked at least 62.5% of a face as buried/outside. Actual screenshots and console on 2026-10-21 showed only 3–8 selected faces, far less than the R01 red contact topology, and failed border extraction on torus and hollow sphere with `Cut boundary branches at a vertex` (0 drawable contours). The original requested algorithm was **vertex-centered**; per-face voting selects torn patches whose vertices may share disconnected survivor fans. This violates R02's intended inspection boundary even though the V14 classifications themselves are numerically confident.

The corrected R02 conservatively selects **all distinct endpoints of all detected contact triangles** (the R01 red-dot set), then the **complete incident triangle fan of each selected vertex**. The outermost red visual ring is intentional and may create a larger-than-minimal excision; it prioritizes simple editable boundary loops and consistent topology rather than exact mathematical cuts. All faces to remove appear orange, and all candidate vertices are logged as stable `VertexId`. Original triangle contact FaceIds are preserved. The orientation of retained faces determines directed yellow rim edges.

## Bounded regularization

The union of closed vertex stars can itself leave two holes meeting at one *retained* vertex. R02 detects that by scanning the **directed cut boundary graph** for any vertex with multiple incoming/outgoing cut edges. It then absorbs the corresponding retained vertex's complete face fan into the removal proposal, and repeats, under finite limits. This is **boundary pinch cleanup, not final zipper/cap surgery**; exceeding the budget explicitly reports `complete=0` and does not approve topology surgery. Any vertex whose *entire* original incident fan has become selected for removal is additionally marked for retirement to avoid isolated zero-face vertices after R03. Disconnected closed components can therefore be proposed for removal with zero boundary loops; such zero-loop cases are distinct from a failed contour extraction.

Hard limits: 512 initial contact vertices, 768 proposed vertices including bounded regularization and orphan cleanup, 4096 proposed faces, 24 passes of rim regularization, 64 directed contours. These apply per **one private candidate**, not across stroke history. A preview is complete only after every proposed vertex has a complete selected face fan, every cut border is correctly oriented and non-branching, and V15 contour enumeration succeeds. Invalid source topology or exhausted budgets are explicit refusals.

## Overlay color contract

| Color | Meaning |
| --- | --- |
| Red dots, dark-red rods | R01 detected triangle endpoints and their edges; the conservative *seed* vertices for excision |
| Orange inset triangles | Full incident-face stars selected for hypothetical deletion, including triangles not geometrically penetrated |
| Cyan dots | Additional rim vertices selected to avoid branching/nonmanifold cut loops |
| Golden rods | Actual directed boundary loops of the hypothetical cut, oriented from retained triangle faces |
| Purple inset triangles | **Informational** V14 fragment classification uncertain on a contacted triangle; this does not control or veto the geometric vertex-star proposal |

V14 face-fragment samples remain informational only. `classified_faces`, `mixed_faces`, `uncertain_faces` and `classification_advisory` help future refinement but are **not** substituted for the vertex deletion plan. Likewise `closure_advisory` is a later V15 cap/zipper fitness assessment, not a reason to discard a valid R02 rim graph.

The freeze remains R01's one-shot, **unpublished** post-Inflate/Refine/Collapse private candidate: no deletions, no CAS publication, no undo/save/checkpoint root mutation. Releasing/repressing LMB does not restart sculpting. Orbit, inside/outside camera, W, G and Escape are available.

## Three mandatory physical acceptance reproductions

```bash
git fetch origin pull/137/head
git switch --detach FETCH_HEAD
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Mesher --repair-preview
./build/Mesher --repair-preview --torus
./build/Mesher --repair-preview --hollow-sphere
```

1. **Default sphere, paired probosci.** Previous failed screenshot showed **8 orange faces, 2 gold loops** and collision freezing at the base of two protrusions *before intended tip-tip contact*. Establish whether R01 contact at the base was genuinely produced by the developing geometry (FaceIds, view from inside), or whether a false positive remains. R02 cannot fix a premature R01 collision decision by changing its selection policy. In the revised R02, both contacted ends should have complete orange incident-face fans and every boundary a simple oriented loop, or else a named finite refusal.
2. **Small-hole torus (R=0.55, r=0.45).** Previous failed `proposed_faces=3`, `boundary_loops=0`, `Cut boundary branches at a vertex`. Revised expected result: a deliberately conservative larger orange patch around the pinched hole, with valid gold loops after any bounded cyan regularization. This is still a *hypothetical excision*—the actual genus-changing closure is deferred.
3. **Hollow sphere with negative-Y mouth.** Previous failed `proposed_faces=8`, `boundary_loops=0`, same branching error. Revised expected result: complete orange vertex-star cuts at the constriction and non-branching gold rims. The inner surface is **not yet detached**, and must not be removed wholesale during R02. Once a later seam actually seals the mouth, a newly isolated redundant interior surface should be deleted *without capping or zippering*.

Capture all `[repair-r02] PREVIEW` lines (especially `proposed_vertices`, `proposed_faces`, `expanded_boundary_vertices`, `orphan_vertices`, `boundary_repairs`, `boundary_loops`, `complete`, `reason`, `closure_advisory`) and at least one inside/outside screenshot. Each malformed or ambiguous case must refuse explicitly, not advertise completion. Verify unchanged published root/undo/save and normal non-contact stamps as in R01. Record tested SHA and CPU CI separately from physical Linux and Windows GPU observations.

The CPU test exercises real crossing cube contacts, a genuinely closed sphere with two synthetic independent contact neighborhoods (explicitly topology-only input to R02), full vertex stars, valid independent rims, no removed vertices surviving on those rims, old-root immutability and frozen worker behavior. The R01 generated fixture and earlier 65-test CI remain prerequisites; CI alone does not prove visual behavior on actual torus/hollow self-contact.
