# V17-M40 — Aggregated private Step resource admission

First independently complete prerequisite of original M13. **PR #89**.
CI acceptance pending; original M13 remains open.

## Real work measured

A physicalExact Step stages Inflate, V6 directional refine, V7 directional
collapse and, when actual V13 source discovery proves real contacts, all
private conforming cut, classified retirement, optional seam refinement
and final certified seam joining. Every `RevisionCommit` clone costs
physical allocation/copy work even if the same partition is cloned
again in a later private stage or is not present in the final published
revision. The resource preflight therefore uses the checked, complete sum
of each private stage's **clonedPartitionBytes** rather than the
final root's retained payload alone. A queued exact c1/c2/c3/M03/d2
Boolean uses the already accumulated `ExactCutStep::copiedPayloadBytes`
from the exact cut, optional refinement, and joined private edits.

For either path, count actual candidate changed stable-ID *references*
(vertex and face journal sizes before ID deduplication) and actual
**final** dirty face-owner partition count. These are distinct from
unique changed IDs or full live mesh size. The three aggregate M40 caps
are `maxPrivateCopiedBytes`, `maxChangedIdReferences` and
`maxFinalDirtyPartitions`. Strict greater-than refuses; exactly equal
must be accepted for a supported real geometric source step. Checked
size_t summation refuses counter overflow. Source validation and local
geometry/classification budgets remain necessary in addition to this
new conservative aggregate envelope. No cost estimate is substituted
for a complete stage's actual copy work.

## Position in the transaction

Both preflight paths enforce the aggregate envelope **before**
the one source-root compare-and-swap. The physical path runs before
final diagnostic Clean credit; an over-budget otherwise-clean candidate
is Unknown and interrupts the M23 consecutive Clean streak. Rejection
rolls back private V6/V7 mirror edits and leaves source certificate,
indexed COW root, dirty renderer partitions, history and published
revision count untouched. The queued path measures and enforces before
speculative mirror journals or indexed CAS. Legacy physicalExact OFF
sculpt is unchanged: resource work can be observed, but these new caps
are opt-in; the existing finite collision and source budgets remain.

Latest attempt telemetry distinguishes `PhysicalStamp`,
`QueuedExact` and `None`; whether a candidate was evaluated,
published, or refused; complete measured copied COW bytes,
changed-ID references, final dirty owner partitions, observed
monotonic wall elapsed milliseconds and the actual refusal reason.
Input rejected before an evaluable candidate MUST return zero
work and `evaluated=false`. Measured pre-CAS refusals retain their
**attempted** work instead of deceptively reporting it as published
or silently erasing the resource cost. CPU elapsed is observation,
not a deterministic budget or promised wall-time upper bound.
Cloned bytes measure COW copying work, not peak RSS.

## Evidence

`testM40AggregateWholePrivateStepResourceBudgets` calibrates against
one genuine M37/M39 overlapping local source, mirrored and normal-driven
physicalExact union; exactly full limits publish and three separately
one-under byte/ID/dirty limits refuse the SAME otherwise-valid source
step without root/source/mirror/history/dirty mutation.
An unrelated invalid FIRST-original FaceId intent refuses early with
zero measured work.

A second, independent source-authenticated real two-tetra
c1/c2/c3/M03/d2 queued exact union calibrates the finite resource
envelope and tests the exact and each one-under category. A true
AsyncSculptWorker FIFO queued positive exposes the same accepted
candidate resource data in `Status.stepResources`.

This does NOT yet claim aggregated winding, collision, arrangement,
contour, seam/join, protected-feature or all-module resource
diagnostics. Those are precisely scoped to M41 and M42. ORIGINAL
M13 later reviews all together; M14 post-CAS exceptions, M15
desktop exposure, V18/V19 remain open. This intentionally does not
claim universal imported Boolean correctness, fixed RSS bounds or
exact CCD.
