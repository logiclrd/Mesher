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

Capture initially supported AUTO_REFUSED only; V2 supports any frozen AUTO.
Use Ctrl+Shift+S for operator desired zipper, Ctrl+Alt+S for caps,
or plain Ctrl+S for unlabelled success/refusal.

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

## 2026-10-22: Capture successful-but-wrong classifier decisions (R06-M07D)

A real physical success may still have wrong M03 semantics. The screenshot
from the symmetric sphere shows two thin green cap patches where the sculptor
expects a joined blue zipper bridge. Do not assume the screenshot alone
proves that the R03 input had two independent linkable rims; a full POST-STAMP
candidate and original R01 contacts are essential.

After **any** frozen R06 outcome, save a case using these shortcuts:

- **Ctrl+S**: save the original auto result without desired topology.
- **Ctrl+Shift+S**: save the identical original candidate annotated
  **PREFER ZIPPER**; use for a successful result that should bridge rims.
- **Ctrl+Alt+S**: save the original candidate annotated **PREFER CAPS**.

This does not re-run an alternative classifier in the GUI or change the
frozen model. It labels the expected M03 **decision** so the precise saved
input can drive later tuning. Archive V2 adds one explicitly validated
`intendedSelection` byte; the reader continues loading V1 exactly.
The observed `expectedClosure` bit remains separate. User annotation is
not an automatic assertion of geometric validity or proof that a particular
zipper passes all M04B quality and symmetry checks.

Successful cases default to `repair-cases/closure-N.r06case`; tagged
capture files use `repair-cases/classification-zipper-N.r06case` or
`classification-caps-N.r06case`; untagged refusals remain
`repair-cases/refusal-N.r06case`. Existing files are not overwritten.
Output includes original AUTO_CLOSED/AUTO_REFUSED and the desired choice.

Both replay modes now print `expected_choice`, `actual_choice`, and
`choice_match`. In headless `--repair-replay-test FILE`, exit 0 means both
the archived original outcome and optional human desired classification
match, exit 2 means a CLOSED/REFUSED change, and exit 3 means an M03
classifier mismatch despite matching CLOSED/REFUSED. A mismatched
classification can therefore be a deliberate red regression until the
planner is fixed, while a future semantic correction becomes explicit.

The initial **five user-recorded V1 real 5K-vertex refusals** were added
without alteration to the new headless CTest corpus:

- refusal-1: CAP q=.082329, 512 refinement steps
- refusal-2: ZIPPER q=.098881, 512 refinement steps
- refusal-3: CAP q=.096550, 512 refinement steps
- refusal-4: mirrored split-orbit finite-budget exhaustion
- refusal-5: CAP q=.014067, 512 refinement steps

These five do not contain a successful misclassification. To test the
pictured mistaken caps, physically capture the next occurrence with
Ctrl+Shift+S, then replay the actual case. Inspect the M03 region's
rim count, paired contacts and area-DP evidence BEFORE tuning V15.
After a future algorithm intentionally fixes a formerly refused case,
review its test expectation instead of silently replacing the archive.

Physical GUI shortcut and Vulkan acceptance remain open, and PR #151 stays
draft until source-based CI and actual Linux/Windows UI testing pass.
