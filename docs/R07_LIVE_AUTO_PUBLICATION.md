# R07 — Publishing certified R06 AUTO repairs in the live brush

## Behavior

`--repair-auto` is now a live worker mode. It does not create an R01
inspection session, freeze the viewport, stall the brush stroke or store
unpublished candidate geometry in renderer buffers.

Every nontrivial stamp is still staged privately by the existing
inflate/refine/collapse worker. The complete post-stamp source is subject
to the same bounded R01 original-contact query used by the authenticated
physical path. If the candidate is clean, it follows the existing ordinary
single-CAS publication path. If transverse contact is proven, R07 runs
R02/R03, independently replans the M03 choice, and invokes the identical
certified R06-M04C automatic closure used in saved-case inspection.

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

- `--repair-inspect --repair-auto`: the historical frozen R01-R06
  candidate preview, case capture and quality/seam diagnostics.
- `--repair-replay FILE`: exact archived read-only reproduction.
- `--repair-step-debug --repair-replay FILE`: synchronous, pausable
  operation snapshots in the replay viewport only.
- `--repair-auto` alone: live, certified, automatic publication; the
  viewport never registers a repair-step callback, and all brush input
  continues normally.

No tests should treat a previously broken AUTO refusal as a desirable
expected result. Historical saved-case outcome bits remain immutable,
but the current full-certificate test now requires AUTO_CLOSED for the
five reviewed recovered cases. Negative tests continue to assert that
invalid/stale/incomplete contacts and mirror journals cannot publish.

## Residual constraints

R06 source fidelity remains a stricter separate concern in authenticated
physicalExact Merge mode. The new mode intentionally does not enable
protected Merge, physicalExact, or OFF-mode collision avoidance at the
same time. All three remain independent opt-in policies.

The screen-visible quality of the finished M24 zipper was manually
accepted on refusal-2. CI can prove the numeric/topological/transactional
invariants, not subjective smoothness.
