# V17-M28 — One physical stamp, one external original-source lifetime

**Independent acceptance:** PR #74, 2026-10-18.
**Original parent:** M22 active cumulative final physicalExact preservation,
which remains OPEN. This is the first of the preplanned flat
M28 → M29 → M30 → original M22 final acceptance tasks.

## Why the flat M22 replan was necessary

The real worker does not publish Inflate, local refine, protected-safe
collapse and optional exact Boolean cuts separately. Its existing
M18 contract publishes one full **original-root** candidate via CAS,
followed by one history/mirror/dirty change. M24–M26 provide
individual-source root evidence, M27 seals a certificate for
each such transition, and M21 requires an exactly sealed
pre-edit predecessor. The old APIs alone could not represent a
multi-stage physical stamp as ONE valid external successor without
either reanchoring the fixed original witnesses or pretending a
private intermediate root was published. M28 resolves that
**read-only** source-chain boundary. M29 must carry certificates
across actual published worker history and checkpoint/recovery;
M30 must enforce the full final M21 fidelity policy BEFORE CAS.

## Certified private stage pipeline

`advanceCertifiedPhysicalStampLifetime(before,inflated,refined,
collapsed,exact,sourceMirrors,...) ` independently checks:

1. **Actual old root:** M27 immutable first-original lifetime
   terminal is the true `VersionedInflateStamp::baseline.before`.
   Every private refine, collapse and optional exact d2 candidate
   must name its exact source root in chronological order. Stale
   or missing roots refuse.
2. **Full bounded Inflate proof:** enumerate every source VertexId,
   FaceId and exact ordered original triangle incidence in BOTH old
   and post-Inflate COW revisions. Their complete sets must match,
   with no added/removed vertices/faces and no rewrites. Only
   explicitly reported `baseline.movedVertices` may have changed
   XYZ; the complete actual old incident-face halo must equal
   `baseline.affectedFaces`. Forged, foreign, duplicate, missing
   or unexpectedly changed IDs refuse. Original vertex and face
   evidence budgets apply to the whole mesh, not an arbitrarily
   selected brush neighborhood.
3. **M24/M25 committed refine:** replay the existing complete
   oriented permanent-ID split, conservative safe flip and
   source-pair/mirror proofs via `advanceOriginalPatchLifetime`.
   No nearest-coordinate transfer authorizes an unrelated source.
4. **M25 collapse:** require the actual prior refined root as
   `collapsed.sourceIndex`. A truly NO-CHANGE collapse must
   have the same result pointer and empty retirement/rewrite/mirror
   journals and consumes zero face-evidence work. Every real
   collapse is independently checked for its actual source halo,
   midpoint, revised faces, tombstones and mirror reciprocity.
   The legacy `stampOriginal` may point to the original stamp
   baseline, so the validator derives the actual source root from
   the separately verified previous stage before calling M25.
5. **M26 full final source-exact Boolean join, when present:**
   the complete source c1/c2/c3/M03/d2 original-root Step must
   start from the accepted post-collapse root. The M26 validator
   rechecks complete source child owners, signed-winding classes,
   protected outer/reversed cavity descendants, certified wholly
   Buried/Outside original retirement, actual vertex welds and
   reflected source orbits. The final result is its validated
   terminal d2 root—never the intermediate cut.
6. **ONE externally usable lifetime:** all stage witnesses and
   consumed source-evidence costs are accumulated under the same
   caller-provided finite cap. The resulting opaque M27 certificate
   then seals one external Step, with parent exactly the authentic
   pre-stamp certificate, original seven-point witnesses and first
   source FaceIds untouched. This is the only result that can
   satisfy M21's predecessor-bound declaration. Private stage
   roots were never published or mistaken for predecessor CAS roots.

No exception produces a usable accepted lifetime; all stages
operate on private immutable COW revisions.

## Actual positive and refusal regression

The `asynchronous_sculpting` CPU suite now includes:

- Two actual successive octahedral Inflate stamps with disabled
  refine/collapse. Both produce one external original-root
  successor each, with second measured deviation strictly greater
  than the first against the ORIGINAL seven witnesses. A fixed
  tolerance chosen between measured values permits the first
  M21 protected Step and rejects the second before a CAS, leaving
  the previously published root unchanged.
- One real normal-driven octahedral Inflate stamp that grows an
  edge and triggers actual committed M24 2-child subdivision.
  All eight nominated original patches keep source-owned
  descendants; the complete split costs two FaceId journal entries,
  and the exact work cap passes while one-under refuses.
- One actual physical normal-driven contact through the original
  V13 source contact discovery, complete c1/c2/c3/M03 and private
  d2 welded union. The entire genuine Inflate/refine/collapse/d2
  sequence forms ONE M21-compatible external successor. Final
  face tombstones are distinguished from source-certified
  complete original-patch retirements; no false promise that
  every retired FaceId means an original source vanished.
- Deliberately omitted movement journal, stale source chain,
  insufficient full original vertex cap, insufficient cumulative
  source-proof cap, and unacceptable second original-surface drift
  all refuse without mutating original readers.

[Substantive CI #39173794290](https://github.com/logiclrd/Mesher/actions/runs/39173794290)
passed GCC, Clang and MSVC warnings-as-errors, all 23 CPU CTest
suites and platform-applicable benchmarks.

## Deliberate scope boundary

**No worker stamp, merge command or checkpoint behavior is changed
by M28.** It does not yet activate M21 at the pre-CAS worker gate,
carry source evidence through durable checkpoint/undo/redo or change
the desktop Merge geometry default. Those are explicitly planned
M29 and M30, with original M22 to close only AFTER the combined
acceptance. M23 contact episodes and original M12 integration
remain open. Seven-point fixed-original witnesses remain finite
one-sided sampled evidence, not continuous two-sided Hausdorff.
