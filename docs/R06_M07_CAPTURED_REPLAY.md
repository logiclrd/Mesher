# R06-M07 — Exact automatic repair refusal capture and replay

Draft PR #150, stacked on #149. Private diagnostics only; no live R07/R08 publication.

## Motivation and fidelity

The former contour dump cannot reconstruct the entire sculpted mesh (surviving
face stars, original contact FaceIds, partition owners, stable vertex/face IDs,
tombstones, allocation watermarks, and source normals). The serialized case
instead captures the full immutable POST-STAMP, PRE-R02 candidate root and
the specific R01 trusted contact pairs after baseline suppression. R02, R03,
R06-M03 planning and R06-M04 closure are re-executed from that evidence by
the current build. Source intrinsic symmetry is saved explicitly and reused
for the strict private mirror geometry audit.

This does not record the original pre-stamp worker root or mouse gesture, nor
does it create an authoritative R07 mirror-ID journal. It is an exact input
for the PRIVATE R06 repair, not a reconstruction of the prior brush stroke.

## Physical capture

Launch with `./build/Mesher --repair auto --repair-dump-contours`. At
`R06 AUTO REFUSED`, press **Ctrl+S**. The console reports
`[repair-r06] CASE_SAVED <path>` or a clearly explained save refusal.

The default destination is the next free filename
`repair-cases/refusal-N.r06case`; each case is one standalone file.
To choose a path at startup use
`./build/Mesher --repair auto --repair-case-out mycase.r06case`.
An existing file is never silently replaced. Ordinary Ctrl+S checkpoint
behavior remains unchanged when the viewport is not frozen; successfully
closed and other frozen repairs still do not save speculative worker roots.

The archive is independently versioned, checksummed and size-bounded. Its
embedded native durable checkpoint retains immutable IDs, partition ownership,
allocator watermarks, and tombstones. The archive also holds trusted contact
face pairs, symmetry mode, recorded success/refusal status and original reason.
Partial writes are staged to temporary files and removed on failure.

## Replaying a later version

`./build/Mesher --repair-replay repair-cases/refusal-1.r06case`

The loader runs the current R02/R03/R06 implementation from the original
candidate and contacts, then opens a frozen Vulkan viewport: green or blue
true closing faces on success, or actual gold R03 boundaries on refusal.
Orbit, dolly, W/G and Escape remain available. Compare the console
`[repair-r06] REPLAY expected=... actual=...` with `AUTO_CLOSED`
or `AUTO_REFUSED`, plus current quality/refinement diagnostics.
`--repair-dump-contours` can also print the loaded case's directed rims.

The replay path does not start an async sculpt worker, mutate a revision,
publish a new history state or write an ordinary checkpoint.

## CI / terminal regressions

`./build/Mesher --repair-replay-test repair-cases/refusal-1.r06case`

This runs the same loaded closure algorithm WITHOUT Vulkan initialization.
It returns 0 if CLOSED/REFUSED matches the archived expectation and 2 if the
outcome changed (or an error exit for invalid/corrupted archives). An intended
future fix that changes a historical refusal to success should update the
test's expected outcome deliberately, not hide the change.

The associated regression fixtures check successful and refused round trips,
stable IDs/positions, partition ownership, tombstones, allocator watermarks,
determinism across repeated runs, corrupt/truncated file rejection, no
overwrites and actual CLI success/refusal replay paths.

## Acceptance still open

Physically produce an AUTO_REFUSED after sculpting, use Ctrl+S, verify one
new .r06case file, then launch a new Mesher process with --repair-replay and
compare the exact contours and reason. Run --repair-replay-test and confirm
no Vulkan startup. Check unique numbering across two captures, corruption
handling, Linux and Windows physical Vulkan views, and future-build
re-execution of the same archived failure. Preserve all existing quality
thresholds, finite work budgets, symmetry and no-publication gates.
