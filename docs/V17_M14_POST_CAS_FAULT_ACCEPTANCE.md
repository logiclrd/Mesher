# V17-M14 — Fail-stop post-CAS auxiliary transaction acceptance

**Independent milestone after the original V17-M13 whole-Step acceptance.**

Substantive [PR #99 CI #39340898434](https://github.com/logiclrd/Mesher/actions/runs/39340898434)
successfully built Linux GCC/Clang and Windows MSVC
with warnings-as-errors, 23/23 CPU tests,
100 repeated checkpoint/worker shutdown trials
and applicable 100K benchmarks. Exact final PR
head and independent merged-main success remain
separate required gates.

## The irreversibility boundary

The one original-root CAS is the externally
visible indexed topology publication. Source
proof, collision/winding, protected 0/+1/0 signed
cavity, fixed-point and off-plane mirrored stable
ID tests and all 38 counted per-Step M40–M45
resource caps happen BEFORE it. Any refusal
there leaves zero new published mesh/history/dirty
owner state and the usual recoverable diagnostics.

After CAS, however, the worker still needs to
apply mirrored source changes and M29 original
history, retain dirty render owners, update
count statistics and eventually furnish a
durable checkpoint. A thrown exception after
the CAS is **not** an atomic declined candidate.
The original Step might already own a new
indexed root while auxiliary state is incomplete.

M14 implements the planned **SAFE TERMINAL
FAILURE** branch rather than claiming reverse
publication is possible. Such an exception:

- is preserved and explicitly classified as
  terminal/irreversible, **not** a recoverable
  invalid caller, geometry or finite budget
  refusal; it never earns Clean hysteresis credit;
- charges actually executed private copied
  COW bytes/changed IDs/dirty owners and
  all 35 M41–M45 source/proof operations to
  ATTEMPTED work, with ZERO complete
  published-success credit;
- quarantines the direct brush: future Stamp,
  queued Boolean union, undo, redo, source
  intent changes, trusted checkpoint and
  snapshot retrieval are unavailable;
- quarantines a real `AsyncSculptWorker`:
  `Status.terminalFailure` and reason expose
  precise terminal failure and attempted
  work; pending uncertain render snapshot
  is cleared, a new command is refused,
  and an in-flight indexed root is never
  exported as a valid frame; and
- leaves existing previously delivered
  immutable external snapshots valid, without
  authorizing their further source advancement
  from a poisoned controller.

The read-side gate is also active **DURING**
post-CAS auxiliary mutation. The
`postCasInFlight_` atomic is set before indexed
CAS and cleared only after successful auxiliary
completion or complete fatal quarantine.
Public `snapshot()` checks gate/failure both
before AND after reading the indexed pointer.
`checkpoint()` refuses when the gate is held.
This closes the small interval where concurrent
readers could otherwise obtain a new root
before its companion history/mirrors existed.

## Real controlled exception matrix

`SculptCollisionSettings::postCasFaultInjection`
is EMPTY by default; it has no connection to
the default-off desktop Merge feature. Tests
alone install a throwing callback for:

1. `AfterRootCas` — indexed topology already
   swapped, mirror/history not yet installed;
2. `AfterMirrorState` — mirror side effects
   applied, history may be missing;
3. `AfterHistoryAppend` — M29 source/history
   recorded but auxiliary finalization pending;
4. `AfterAuxCommit` — auxiliary mirror
   journals committed but final completion
   diagnostics/publication not yet confirmed.

All FOUR actual post-CAS points are exercised
against BOTH genuine M39 72-original protected
reflected 4D physical source Contact and
independently authenticated asymmetric queued
two-tetra c1/c2/c3/M03/d2 exact Boolean.
Every failure must be reported as terminal,
preserve measured attempted source work,
charge ZERO complete accepted publication,
withhold checkpoint and root, and reject
subsequent operations. The test also holds a
real queued CAS open using one producer
thread while a reader tries `snapshot()`
and `checkpoint()` before the callback
is released; neither can expose a
half-committed root.

A genuine async worker fatal injection proves
the error is accessible in worker `Status`,
not hidden by a thrown exception from the
background thread, and its pending renderer
mailbox is cleared with zero speculative
history or accepted published-cost credit.

## Filesystem I/O is independently recoverable

The asynchronous checkpoint writer operates
AFTER a fully accepted and coherent source
transaction. The test saves to a deliberately
nonexistent parent directory, proving a real
failed file write is reported through
`CheckpointStatistics.failed` without
rolling back or corrupting the accepted
indexed COW root, protected M29 history,
current dirty owners or published work ledger.
It then writes a valid checkpoint in the
same worker session; the completed write
contains a V13 original-source requirement
but NOT a fabricated sealed opaque M29
first-original proof, consistent with M29.

## Exact scope and next task

This acceptance specifically chooses a
fully reported, fail-stop **terminal**
outcome for an irreversible post-CAS
exception. It does not promise reverse
CAS rollback, general process-power-loss
transaction durability, generic coincident
XYZ source identity, unsupported arbitrary
multi-body Boolean capability, exact CCD,
global Hausdorff protection, bounded
wall time or process peak-RSS enforcement.
The original root/mirror/topology selection
predicates and work budgets were not relaxed.

**NEXT:** V17-M15 default-OFF desktop
`Merge geometry` opt-in, targeted Linux/
Windows Vulkan, symmetry, picking, history,
storage and refused-stroke acceptance.
V18 Off-mode and V19 broad acceptance
remain independent and unchecked.
