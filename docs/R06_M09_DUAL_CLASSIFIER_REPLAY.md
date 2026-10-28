# R06-M09 — Honest classifier A/B replay and frozen top-left HUD

A saved completed `classification-zipper-1.r06case` may have originated
from a successful area-based cap. Selecting `--repair-classifier proximity`
can produce a different, genuine M03 zipper on the **same candidate**.
M04C previously independently re-planned with the **default area
classifier**, saw a different cap/zipper set, and falsely diagnosed
`stale or forged M03 closure choice`. That was a *verification policy
mismatch*, not proof that the saved candidate lacked replay evidence.

M04C now retains independent source-R03 cut/face-ID/contact provenance
checks and regenerates M03 choices with the SAME selected classifier
policy supplied to the actual planner. A modified/forged plan remains
a refusal under either classifier. This fixes the false M04C mismatch.
It does not guarantee the newly selected geometry passes subsequent
M04B triangle-quality (q>=0.10), mirrored seam, self-intersection,
manifold or material classification tests; those genuine failures are
still reported without publication.

## Freezing or replaying

```bash
./build/Mesher --repair auto --repair-classifier area
./build/Mesher --repair auto --repair-classifier proximity

./build/Mesher --repair-replay repair-cases/classification-zipper-1.r06case --repair-classifier area
./build/Mesher --repair-replay repair-cases/classification-zipper-1.r06case --repair-classifier proximity
```

After M03 has run once on the frozen R02/R03 cut, the R06 viewport
shows **two independent lines in the upper-left**:

```text
AREA: CAPS
PROXIMITY: ZIPPER <---
```

Each line reports the **planned M03 topology**, not actual accepted
geometry or subjective correctness. `<---` marks the selected mode.
The lettering is green if both **valid complete plans** have the same
actual cap set and same unordered zipper pairs. It is red if those
planned choices differ. If either M03 plan cannot be built, the status
`UNAVAILABLE` is displayed in normal white, not as a false green
agreement. These labels do not imply a repair is right or wrong.

The top-right FPS/stamps/repair status block is independent and unchanged.
A successful selected closure still shows green faces for caps or blue
for zippers; a later M04 rejection still shows actual R03 gold cut.
The secondary M03 evaluation NEVER builds its own private M04 surface,
never publishes a root, and does not change the selected result.

Headless replay prints a `[repair-r06] CLASSIFIERS` line even when M04
refuses. `--repair-replay-test` preserves archive-based CLOSED/REFUSED
exit 2 and human classification-intent mismatch exit 3 behavior:
switching classifiers can intentionally produce either a new outcome
or a different selection, and these are diagnostic observations.

## Regression and physical acceptance

The exact originally reported case remains local to the operator and
was not in the repository as of this milestone. The known committed
V1 physical cases 1 through 5, plus synthetic V2 cases, exercise
replay under both classifiers in CI. CPU regressions check the two
summaries arise from identical candidate/cut, M04C verifies the selected
policy, and forged plans are still rejected under either policy.

The attached user case is especially important for physical validation:
area must show CAPS, proximity ZIPPER, with red lettering, and the
stale/forged M04C explanation must disappear. If the zipper still
refuses, its *new specific geometry reason* should be used as the
basis of a separate fix. Any successful zipper remains private and
unpublished until later R07 certification is implemented.

Acceptance requires GCC/Clang/MSVC tests plus a physical Linux and
Windows Vulkan upper-left HUD check. Draft until approved.
