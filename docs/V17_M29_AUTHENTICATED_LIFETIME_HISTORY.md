# V17-M29 — Authenticated first-original lifetime history and safe recovery

**Milestone status:** Accepted in PR #75, October 18, 2026.
**Baseline:** V17-M28 merged to main at `e3ce3891aeb3fc487a8e94a3d835efd79a128bea`.
**Substantive GitHub Actions:** [#39176592637](https://github.com/logiclrd/Mesher/actions/runs/39176592637), GCC/Clang/MSVC warnings-as-errors, 23 CPU CTest suites and applicable benchmarks, all successful.

## Why this is independent from M30

M28 proved complete multi-private-stage physical stamps (Inflate,
refine, collapse, optional d2) against the FIRST original permanent
FaceIds, with one externally certified successor per stamp. M21
separately binds an explicitly designated original-source intent
certificate to its exact predecessor. M29 adds the **ownership,
history, eviction and recovery boundaries** for carrying accepted
opaque M27 lifetimes through actual published histories. It does NOT
activate worker pre-CAS auditing; that is the separately planned M30
objective, and original M22 remains unchecked.

### Direct-source history ownership

`RevisionHistory` now optionally accepts a sealed
`OriginalPatchLifetime` for its first root. Every tracked
`append` requires a separately sealed candidate with:

- The exact previous and new immutable COW root pointer identities;
- M27's full first original FaceId/vertex/witness/edge-scale record;
- The *direct* authenticated previous lifetime certificate, not
  an independently restarted root or a stale/sibling branch;
- One new external source transition with nondecreasing cumulative
  face-source evidence and preserved explicit Buried/Outside retirement.

The check is made **before** discard of the redo branch, byte
accounting changes, or publication to the history list. Missing,
forged, stale, mismatched and cross-source candidates fail closed.
The earlier root and remaining redo/undo entries stay unchanged.
The same immutable proof is attached to each undo/redo transition:
an undo never silently uses the later source lifetime, and a redo
cannot reset its first-original witnesses. Accepted branching must
be proved against the *current undone parent*. Default untracked
history callers retain their existing behavior with a null proof.

### Bounded old-root retention

A prior M27 source certificate used a **strong** pointer to its
predecessor. In a long sculpt history that could pin every previous
COW index after the bounded `RevisionHistory` discarded it,
undermining retirement. M29 replaces that link with a weak
predecessor and verifies it while the immediately preceding
lifetime is still held by the active edit. M28 accumulates
independently certified reflection flags during each private
stage rather than traversing expired parent nodes later.

Every accepted lifetime necessarily keeps the **first original**
COW root for invariant witnesses, its **current** terminal root,
and its immutable owner graph. It no longer keeps every
intermediate Step's root transitively. Bounded-history tests
verify a retired intermediate weak root actually expires while
the first original reference remains valid.

### Protected binary checkpoint and recovery boundary

Legacy checkpoint version 12 remains byte-compatible on read and
write when no original-source fidelity is required. A protected
write uses version 13 and a checksummed `originalFidelityRequired`
flag. The optional sealed lifetime is usable only for a trusted
**in-memory** handoff, and the writer independently verifies that
its terminal root matches the saved mesh. It intentionally does
NOT serialize an unverified reconstruction of opaque M24–M28
journal authority.

A version 13 disk reader restores geometry, stable IDs,
tombstones, mirror correspondences and the protected-session
requirement, but returns **no forged certificate**. Trying to
restart protected `physicalExact` with that recovered terminal
mesh and no independently authenticated original reference
throws before a worker can accept a stamp or reset its first
original baseline. Re-saving a protected checkpoint without
proof carries the requirement forward rather than downgrading
to an unprotected version 12 record.

This is **safe fail-closed binary recovery, not resumed protected
sculpt editing**. A future positive disk-resume format would have
to preserve and independently verify the actual first-original
topology and a complete source-proof transcript, not merely
recompute a checksum or replace the first root with the recovered
terminal geometry. M29 makes this limitation explicit rather
than suggesting that a terminal-only file could prove its own
cut/collapse/weld ancestry.

### Acceptance

The persistent-history CPU regression uses real successive
physically committed source-owned COW stamps with immutable
first-root witnesses, then exercises invalid stamped source
work, missing/forged/stale proof, undo and redo, genuine
authenticated branch after undo, bounded evictions with
expired intermediate roots and surviving first original proof.

The checkpoint acceptance verifies version-13 marker preservation,
missing certificate after binary reload, fail-closed protected
physicalExact worker construction, and positive trusted
in-memory handoff. The full existing version-12 checkpoint,
mirrored rollback, worker FIFO, picking, history and retirement
tests remain green under the same three-platform run.

**Still open:** V17-M30 actual terminal pre-CAS worker audit and
rollback/FIFO/reflected validation; original V17-M22 end-to-end
acceptance; V17-M23 hysteresis and original M12 integration.
The desktop Merge geometry control remains default-disabled.
