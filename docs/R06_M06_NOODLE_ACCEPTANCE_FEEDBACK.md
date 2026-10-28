# R06-M06 — Physical feedback: noodle contact classification and quality

**Observed physical Linux issue (2026-10-22).** In five ad-hoc "smash the noodles" tests
with `--repair auto`, three selected independent caps rather than a bridge.
One refused, correctly leaving a frozen R03 open cut and gold rim. In the refusal
the R01 query was complete (27 trusted face pairs, 18 participating faces);
R02 proposed two excision components, two directed loops (18 vertices each;
perimeters 0.827), and 68 removed faces with no repaired branch vertices.
R03 removed those faces and left 36 boundary edges. M03 returned status 0
(`Ready`), but M04B refused a triangle with `q=0.068893` below `0.10`,
despite its longest-edge ratio being only 0.215962. This is **quality**, not
an edge-length-bound failure; disabling or lowering quality is prohibited.

The torus and hollow-sphere cases passed repeated physical auto repairs,
including removal of the hollow sphere's certified redundant inward shell.
These are important positive results, but R06-M06 is NOT ACCEPTED yet.

## Architectural diagnosis

M03 still calls the V15 circular-frustum fitness estimator. It strongly
penalizes same-survivor zipper options (requires 55% area savings rather than
20% for distinct exterior shells). When V15 finds two same-survivor contours
nonplanar, elongated or otherwise ambiguous, M03 **silently defaults to two
independent caps**. A tentacle-to-tentacle weld and torus handle removal can
both involve two same-survivor rims, so neither lowering the 55% penalty
globally nor zipping all trusted contacts is topologically defensible.
M04B selects a conforming edge to bisect repeatedly; midpoint subdivision
cannot always improve a triangle's aspect ratio or repair a poor original
diagonal. An isolated q<0.10 failure must never return a published root.

## Flat remediation plan

All M06 peers have been recorded in `todo.md` **before** implementation:
M06A diagnose, M06B correct the topology evidence/policy, M06C improve
quality-bounded retriangulation, M06D rerun full physical/matrix acceptance.

### M06A diagnostic acceptance

Expose per independent region *even when M04 refuses*: the exact M03
decision path; V15 ready choice versus ambiguous cap fallback; rejected
zipper savings versus same-/different-survivor threshold; cap versus
zipper selected face counts, true R01 patch links, rim centroid separation,
normal dot product, symmetric nearest vertex distance in local edge units,
and approximate cap/zipper frustum areas. These metrics are **advisory**
and not certified closure geometry. Keep forced modes unchanged.
When M04 rejects a triangle, report whether it belonged to a CAP
or ZIPPER, its stable input/staged vertex IDs, quality and bound,
length ratio and consumed finite subdivisions. The existing gold
R03 refusal rendering and frozen original/source root are mandatory.
No change to M03 topology selection or M04 quality thresholds is
authorized under M06A.

### M06B / M06C work not yet accepted

M06B must test a real two-proboscis close facing contact (not merely
a fabricated pair of near-coincident XYZ loops), and an actual
pinched torus: the first should prefer a certified short bridge
when warranted; the second must still prefer caps and remove the
tunnel. Preserve all trusted R01 face-provenance restrictions and
explicit ambiguity/tie refusals.

M06C must test bounded alternative triangles or safe interior edge
flips/conforming retriangulation that improve genuinely skinny
geometry, not just add more midpoint subdivisions. Maintain
q>=0.10 for both new cap and zipper triangles, old-survivor
quality threshold, stable ID/tombstone semantics, full closed
manifold/vertex-fan audits and no worker/history/checkpoint changes.
Quality failures must still refuse.

## Additional physical cases (2026-10-22, same Linux environment)

**Self-colliding single noodle:** the full incident-star R02 excision
combined what would, under a strict Boolean, be two local holes into a
long, narrow, curling boundary. The screenshot shows an actual gold
open rim from outside and inside. M03 selected two caps, but R05's
bounded projection search failed:
`R05 no simple admissible projected disk after bounded orientation search:
R05 contour projection overlaps itself`.
No geometric surface was published. A topologically valid 3D loop does
NOT necessarily admit an injective planar projection. A 3D-cap fallback
MUST validate generated triangles against the source and each other;
a loop need not admit an embedded disk at all. M06C owns this work,
not the earlier M06A scoring instrument.

**Strongly facing noodle mouths incorrectly capped:** one region,
two distinct excision patches, 33 trusted crosspatch face contacts;
same surviving shell, `normals_dot=-0.998`, nearest gap 1.624 local
edge units, centroid separation 0.058. V15 favored a zipper worth
**50.3887%** area savings, but M03 downgraded it because
`0.503887 < 0.550000`. Actual two caps passed M04 (quality 0.325)
but were visibly the wrong topology.

**Correctly selected zipper failed quality:** one directly linked region,
two loops, `normals_dot=-0.994`, nearest gap 2.053 local edges,
area savings 66.6201%, and exact M03 zipper selection. M04B then
refused **ZIPPER** triangle with vertices `5822,5737,5821`,
`q=0.078209` vs 0.10, `length_ratio=0.229595`, after
**512/512** midpoint refinements (512 inserted vertices).
The failure is **not** an oversized edge and cannot be resolved by
loosening any threshold or blindly adding subdivisions. M06D owns
quality-increasing legal retriangulation.

## M06B provisional facing-contact classifier and replay capture

For *exactly two loops* with distinct removed source-face patches,
a direct trusted R01 link, one common surviving component, nearly
opposing oriented rim normals (dot <= -0.95), symmetric nearest rim
separation <=2.5 local edge lengths, and an otherwise admissible V15
zipper option, the same-owner minimum area savings is provisionally
0.45 (previous general threshold 0.55). Other same-owner regions
retain the 55% requirement, disconnected/no-provenance regions never
receive a proximity-only pairing, and the final M04 quality/manifold
audits remain entirely unchanged. This is *not* proof of the user's
precise configuration, and M06B remains open pending real physical
comparative acceptance on the noodles and torus. CPU fixtures include
one single connected U tube with synthetic trusted FaceId links and
two close opposing true oriented rims: the old policy caps, while
the new provisional policy selects a zipper; removing the link
still results in caps.

Run `./build/Mesher --repair auto --repair-dump-contours` to emit the
exact R03 directed contour vertices as
`[repair-r06-contour] loop=... ordinal=... id=... xyz=x,y,z
surviving_face=...`, with **17 significant digits**. This output
is opt-in, console only, and does not publish or checkpoint the
private mesh. Capture the complete lines on a naturally arising
nonprojectable folded contour so M06C can construct a genuine
deterministic test before changing R05's cap behavior.

The actual user-provided R06 refusal has not been captured as a
replayable checkpoint or deterministic stamp sequence. Do not
claim the exact contact was reproduced by a synthetic test.
