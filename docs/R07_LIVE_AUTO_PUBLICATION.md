# R07 — Publishing certified R06 AUTO repairs in the live brush

## Behavior

Certified R07 AUTO is now the **default** for ordinary interactive mouse
Inflate; no `--repair` selection is needed or accepted. The worker does not
create an R01 inspection session, freeze the viewport, stall the brush
stroke or store unpublished candidate geometry in renderer buffers.
Diagnostic `--repair-inspect` remains an explicit frozen, private mode.
The historical forced-cap/zipper selectors have been retired.

Every nontrivial stamp is still staged privately by the existing
inflate/refine/collapse worker. R07 performs **changed-neighborhood**
contact discovery: it uses current moved/inserted vertices and changed
faces as query seeds against the COMPLETE candidate BVH, so any new
intersection touching the stamp is found even if the opposing face is
outside the brush region. Existing contacts already present in the
immutable before root are excluded by a second bounded query. The full
post-state stable-ID and exact-reflection mirror map are still audited.
This is a separate finite-work policy from protected Merge's strict
whole-original V13 source scan, which remains unchanged.

A source-clean candidate follows the normal single-CAS publication path.
If new transverse contact is proven, R07 runs R02/R03, independently
replans the M03 choice and invokes the identical certified R06-M04C
automatic closure used in saved-case inspection.

The live mode chooses the accepted unified AFM zipper for zipper regions
without requiring the diagnostic `--repair-unified-zipper` flag. Cap
regions retain the updated evolving-corner-normal advancing front.
There is no old zipper 0.5*target boundary density pass: the two M24
shape templates and existing hard quality/length admission are used.

## Publication boundary

After R06 returns a fully certified closed index, a separate read-only
R07 audit compares the entire source/final stable-ID ledger, with a finite
element bound. Surviving vertices MUST keep original XYZ, every added
vertex MUST be referenced by a final face, and every new/retired original
mirror orbit MUST be reciprocal, complete and reflected exactly about
X=0. Old surviving original IDs keep their prior mirror correspondence.
The audit derives changed faces/vertices and a canonical net journal
for the existing source-backed worker mirror maps.

All edits remain private until the brush's ordinary single immutable
indexed-root CAS. A successfully published AUTO replacement is ONE
history revision, so the existing undo, redo and checkpoint paths all
observe precisely the certified result. Neither the R03 open patch nor
a partially refined face ledger is made visible to the renderer.

Failure in contact classification, source/excision/plan checks, final
geometry, mirrored ID audit, bounded work, admission, or user validator
rejects the ENTIRE proposed stamp and rolls back provisional mirror edits.
The ordinary brush is not frozen by geometric refusal; it can accept
later input. Runtime faults after CAS retain the worker's existing
terminal-failure quarantine.

A recovered checkpoint carrying authenticated first-original lineage
continues to enforce its legacy proof requirement: R07 does not silently
re-anchor or bypass that protected source. Live R07 on such a checkpoint
cannot publish a history transition unless its first-original
publication proof is also supported.

## Opt-in diagnostics stay separate

- `--repair-inspect`: frozen R01-R06 AUTO candidate inspection, case
  capture and quality/seam diagnostics.
- `--repair-replay FILE`: exact archived read-only reproduction.
- `--repair-step-debug --repair-replay FILE`: synchronous, pausable
  operation snapshots in the replay viewport only.
- No selector: live, certified, automatic publication; the viewport
  never registers a repair-step callback, and brush input continues.
- `--repair-preview` and `--repair-excise`: private R02/R03 intermediate
  diagnostics, not fallback cap/zipper modes.
- Explicit `--merge-geometry` and `--avoid-collisions` retain independent,
  protected interaction policies; neither silently enables a competing
  closure strategy.

No tests should treat a previously broken AUTO refusal as a desirable
expected result. Historical saved-case outcome bits remain immutable,
but the current full-certificate test now requires AUTO_CLOSED for the
five reviewed recovered cases. Negative tests continue to assert that
invalid/stale/incomplete contacts and mirror journals cannot publish.

## Trace isolation

The accepted M23B zipper can generate detailed accepted-vertex placement
derivations and face-ID maps. In live production these are strictly disabled:
`MixedClosureStageSettings::emitTrace` defaults to false. The same
source-backed constructor remains in use without snapshot creation or
per-vertex text emission. Explicit private inspection and saved-case replay
set `emitTrace=true`; archived diagnostics tests request it explicitly.
The M24 per-split shape trace is controlled by the same flag.

## Residual constraints

R06 source fidelity remains a stricter separate concern in authenticated
physicalExact Merge mode. The new mode intentionally does not enable
protected Merge, physicalExact, or OFF-mode collision avoidance at the
same time. All three remain independent opt-in policies.

The screen-visible quality of the finished M24 zipper was manually
accepted on refusal-2. CI can prove the numeric/topological/transactional
invariants, not subjective smoothness.

## Default-sphere regression (R07)

The earlier R07 clean-stamp test used a 600-vertex sphere with a tiny
deformation. A normal default 5000-vertex mirrored sphere with the desktop
strength and brush radius failed to publish, because scanning every triangle
on each stamp exhausted the old V13 `maxBoxTests=524288` work budget.
The accepted fix seeds the BVH from the stamp's changed faces and validates
their complete collision neighbors, with independent R07 finite limits.
To avoid repeated tree walks, up to sixteen nearby changed face AABBs
are batched into each broad-phase traversal. Three-dimensional Morton
ordering groups spatially nearby triangles, including around curved
and mirrored surfaces, without growing pathological one-axis AABBs.
The exact per-face triangle-pair classifier is unchanged; the tests
compare the complete contact list against the original per-face query.

The localized detector decides whether a *new* contact exists. When it
does, the repair path obtains a separate, fully bounded **whole-source**
collision graph, using the batched broad phase, before R02/R03/M03
excision or R06 certification. Whole-source discovery is therefore
not performed on an ordinary clean sculpt stamp. Protected Merge
retains the original unbatched, strict full-source query.
Protected exact Merge continues to require the V13
`splitTransverseTriangleContact()` finite segment and two exact local
triangle patches. That **does not apply to AUTO's approximate excision**:
R02 removes complete vertex stars from reported intersecting face pairs,
and R03/R06 reconstruct and certify the resulting boundary. Requiring
exact original-face split patches prevented genuine AUTO repairs on a
single torus. For R07 only, unambiguous intersecting face pairs are
admitted as whole-face contact evidence, even when the V13 split-patch
construction is inapplicable. A tangent contact is not automatically
accepted as a published repair: the complete source graph, R02/R03
excision, M03 choice, certified R06 geometry, exact mirrors, final
manifold, collision and material checks remain mandatory. Ambiguous
pairs and failed closure still refuse atomically.
The test must require a published changed root with zero refusals; it is
not acceptable to lock in the previous silent rejection as passing behavior.
Live AUTO worker status shows applied, rejected, published and repaired
counts, and logs the complete refusal when one occurs.

## Live acceptance regression

The exact 950-vertex source torus's inner rim, displaced by a genuine
normal-driven Inflate stamp at strength 0.1, must trigger R07 contact,
produce a certified AFM cap repair, publish exactly one changed root,
and support undo back to the original. This checks the complete
detector-to-worker publication route, not just a saved refusal-case
replay. Retain the test as a **positive passing invariant**: a V13
split-patch refusal on this source is not acceptable AUTO behavior.
