# V17-M47 — Bounded source-authenticated deferred desktop Merge discovery

**First independent prerequisite of unchanged original V17-M15.**
The flat M47→M48→M49→original-M15 plan was recorded in
`todo.md` before starting implementation. This milestone
changes only the worker-side capability. The actual default-OFF
Vulkan Merge UI remains for M48 and combined desktop
acceptance for M49/M15.

## One explicit, immutable, exact source command

A producer calls `AsyncSculptWorker::mergeDiscoveredContacts`
with the precise immutable **expected indexed root**
seen at selection and **nonempty explicitly nominated**
first-original FaceIds. No pointer/later-terminal
coordinate match can select a source. The producer
does not query collision pairs or do large source work;
one new FIFO `DiscoverMerge` command runs only on
the serial worker. It refuses an obsolete selected
root before touching geometry.

The worker verifies all selected IDs against the
actual *initial* trusted original `FlatMeshIndex`
face record, forbids duplicates, invalid IDs and
unbounded declarations, and requires still-current
original-source M29 protected fidelity. A separately
trusted in-memory recovered **unadvanced** first
root may also supply explicit certified first-original
FaceIds when the selected original list agrees
EXACTLY with its sealed original anchor faces.
This admits a legitimate reflected source with its
OWN reciprocal mirror pairing and no assumed
sphere vertex-order convention. An arbitrary disk
V13 checkpoint has the original-source REQUIRED
marker only and cannot create this authority.

`VersionedSculptBrush::discoverLiveMergeContacts`
clamps every caller-supplied work allowance to
the preset finite V13 original full-root discovery
limits: maximum source vertices/faces, six
collision-query cost categories and full section
checks. It verifies the complete current original
lifetime and invokes the already accepted
`assessPostSculptOriginalContacts` over *every*
live vertex/face, not a selected dirty subset.
Sculpt symmetric roots must have an exact stable
reciprocal mirror involution including X=0
self-mirrors; the assessor refuses ambiguous,
tangent, unsupported, non-transverse or truncated
contact graphs. A **Clean** graph with zero
intersections is an explicit no-contact result,
not synthetic Boolean success.

Only fully supported source-qualified contacts
reach the existing `mergeExactContacts`, with
the same exact expected root, nominated FIRST
FaceIds, M03/d2 fully classified surgery and
M40–M46 original-source/resource caps. There
is exactly ONE candidate final-root CAS and
one bounded history entry on successful joining,
never an intermediate cut root or speculative
renderer mailbox. Failure leaves the indexed
root, initial picker, original source/provenance,
mirror IDs, renderer dirty owners and history
untouched. Deferred live-discovery outcomes
have separate FIFO status counters and a reason;
they never claim unperformed COW or geometry
as attempted work.

## Positive and negative regression

`testM47BoundedAuthenticatedLiveMergeDiscovery`
has two distinct truly supported positive
fixtures: (1) the asymmetric intersecting
two-tetra original, and (2) its reflected
double-section source with explicitly sealed
reciprocal mirror pairs and first-original
anchor certification. Both publish precisely
one original-root history entry and correct
dirty-owner immutable renderer snapshot;
the original picking root remains unchanged
as a held reader. A stale repeated selected
root must refuse without silently rebasing.
One undo returns to the earlier immutable
indexed root and redo returns to the same
joined root without rerunning geometry.

Separate refusal cases verify legacy OFF-mode
does not gain opaque authority, an impossible
or absent original FaceId is not tolerated,
a finite vertex allowance below the complete
root rejects rather than truncates contacts,
the same worker subsequently accepts a
fully bounded positive union, a genuinely
clean separated source yields no union,
and disk-recovered V13 source cannot be
promoted into an authenticated history.
No tests invent an inferred global coordinate
weld or relabel unrelated coincident surfaces.

## Evidence, exclusions and next steps

Substantive [PR #100 Actions #39345076059](https://github.com/logiclrd/Mesher/actions/runs/39345076059):
**SUCCESS** all 3 compiler jobs, full Linux/Windows
Vulkan build, Windows MSVC warnings-as-errors,
23/23 CPU suites and applicable repeated
checkpoint/worker shutdown stress and benchmarks.
The exact documentation-final PR head and
independent merged-main push CI are
additional mandatory gates.

**NEXT — V17-M48** supplies the visible
default-OFF desktop switch, explicit
Vulkan picking/source-selection path and
bounded queued request routing. **M49**
will exercise the CPU-testable UI mode/
command state machine and targeted Vulkan
build acceptance, then **original M15**
integrates the full criterion-by-criterion
review. M48/M49/M15 are deliberately
unchecked. V18 default-off collision
constraints and V19 broader GUI/robustness
and human-observed runtime acceptance
remain separate. No universal arbitrary
multi-body Boolean, exact CCD, general
3D nonmanifold welding, real-time bound or
peak RSS cap is implied.
