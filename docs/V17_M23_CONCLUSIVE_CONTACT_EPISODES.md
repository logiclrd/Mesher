# V17-M23 — Conclusive original-contact episodes

**Original milestone accepted:** PR #82, substantive
[GitHub Actions #39200600909](https://github.com/logiclrd/Mesher/actions/runs/39200600909).
Linux GCC, Linux Clang, Windows MSVC warning-as-errors,
all 23 CPU suites and applicable 100K benchmarks PASSED.

## Meaning of the episode decision

This is a conservative, worker-visible **diagnostic state** over
actual fully classified physical collision/source checks.
A conclusive Contact starts or extends the *same* active episode.
Only **three consecutive conclusive and otherwise accepted Clean
decisions** end it. Unknown, failed validation, ambiguous contact
classification and finite work exhaustion do not close the episode
and interrupt the current consecutive quiet streak. Recontact
before closure retains the same episode; Contact after the
third consecutive Clean begins a NEW episode.

The older V16 tracker counted overall Clean checks but left
`quietRun=2` unchanged across an Unknown, allowing
`Clean, Clean, Unknown, Clean` to falsely close a contact
episode. M23 resets `quietRun` and the consecutive
`contactRun` on Unknown, preserving `active=true`.
On the third successive accepted Clean it increments
`closedEpisodes`, clears the active flag and resets
the temporary quiet run. `episodes` counts actual starts;
`closedEpisodes` counts actual endings, exposing the
difference for diagnostics. Constructor still rejects a
zero quiet threshold; default remains exactly THREE.

## Full-pipeline observation and refusal rules

Both opt-in paths in `VersionedSculptBrush::stamp`
collect ONE pending contact classification per independently
evaluated proposed Step. `physicalExact` calls
`assessPostSculptOriginalContacts` on the actual candidate;
legacy selected V16 collision evaluation calls the complete
private classified planner. A V13 `CompleteTransverse`
or a fully accepted V16 contact candidate is proven Contact
even when the subsequent Boolean source weld or terminal
validator fails. It may extend the diagnostic episode, but
**never** approves the weld or skips the refusal. A fully
classified Clean is ONLY credited after all downstream
validator/complete terminal M21/M29 source fidelity checks
have passed. Any subsequent rejection demotes that
Clean observation to Unknown; incomplete contact discovery,
uncertain original owner, violated source budget, failed
closure/classification and invalid caller FIRST-original
FaceId intent cannot supply Clean evidence. Early rejected
caller intent also records an interrupted Unknown without
running an unnecessary private candidate.

This tracking does **not** memoize a Clean interval and
does not bypass a single fresh V13 original-root query,
private c1/c2/c3/M03/d2 cut/closure validation,
post-d2 full root collision or FIRST-original M21/M29
witness/descendant/anchor audit. The boolean and physical
paths still fail closed when exact source provenance is
unavailable; speculative controller maps, immutable COW
reader/picker roots, dirty owners and undo/redo histories
remain protected by the already accepted M28–M30
publication gates. No contact-based shortcut has been
added to geometry or source policy.

## Real asynchronous acceptance

`testM23ConclusiveEpisodesAndReal4DFIFO` exercises both
the state machine and the *real* symmetric M11 physical
worker. With two independently source-owned mirrored
tetra tower pairs originally DISJOINT, a genuine 4D
Begin/End physically creates a supported intersection
and publishes the first complete c1/c2/c3/M03/d2 union:
`episodes=1`, Contact=1, `active=true`. The test
takes its subsequent exterior brush target from a
**live vertex of the welded revision**, rather than
a possibly retired imported XYZ point. Two separate
normal-driven 4D exterior stamps genuinely deform
mesh vertices and complete independent V13 Clean
classifications; their terminal COW roots and dirty
partitions are consumed by the renderer mailbox.
A deliberately foreign FIRST-original FaceId request
then refuses before sculpt; it contributes Unknown,
not Clean, resets the quiet run, changes no live root
and adds no dirty owners. Two newly classified Clean
stamps leave the original episode ACTIVE; a third
consecutive accepted Clean closes exactly once.
A genuine 4D source-weld on the SECOND disjoint tower
recontacts the mesh and opens `episodes=2` while
`closedEpisodes=1`, with a new distinct authenticated
terminal history root. The test does not fake
classifier results or choose contacts from XYZ alone.

The existing M11 broad-source physical FIFO regression
also proves that an actual V13 section-work budget
exhaustion produces Unknown with no inferred
Clean/Contact or published root. The NEXT valid,
bounded and intended mirrored physical source union
then opens its own proven Contact episode. The older
V16 `testContactEpisodeHysteresisNeverCachesApproval`
was updated: its previous expectation that Unknown
preserves two Clean credits directly contradicted the
original M23 goal; it now checks fresh consecutive
closure, `closedEpisodes`, and genuine recontact.

The entire existing 23-suite CPU suite, including all
SIXTEEN successful M11 alternating mirrored physical
source unions, independent coincident FaceId provenance,
cavity protection, M22 cumulative source fidelity and
true source-authenticated physical/queued FIFO operation,
passes on all three compilers.

## Non-goals and remaining acceptance

Episode state is diagnostic rather than Boolean weld
authorization or stable source cache. It does not claim
continuous two-sided Hausdorff, all imported-mesh
source semantics or swept-volume CCD. M23 does NOT
complete the original M12 integration: that separately
requires M19–M23 in repeated same-patch unions with
exposed and reversed-cavity walls, symmetric protected
outer walls, independent budgets and real recovery.
M13 telemetry, M14 injected post-CAS exceptional
atomicity, M15 default-off desktop Merge Geometry
activation, and V18/V19 remain OPEN.
