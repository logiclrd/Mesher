# R06-M12 — Edge-visible cap/zipper inspection, optional quality warnings

## Commands
```bash
# Normal selected auto quality/collision/manifold policy is unchanged.
./build/Mesher --repair auto --repair-classifier area
# Opt in to a second, untrusted preview after strict quality refusal.
./build/Mesher --repair auto --repair-quality-warn
./build/Mesher --repair-replay repair-cases/refusal-1.r06case --repair-quality-warn
# Headless STRICT result/exit code is unchanged; warning preview is diagnostic:
./build/Mesher --repair-replay-test repair-cases/refusal-1.r06case --repair-quality-warn
```
`--repair-quality-warn` is rejected outside R06 AUTO live/replay. It does
not change the persistent saved case schema, classifier decisions, or the
production M04C certificate. The original strict AUTO path ALWAYS runs first.
If it fails for a quality/refinement issue and the user opted in, a SECOND
private staging transaction may produce an untrusted closed-index preview
with bad triangles. Even when the preview happens to have a closed incidence
graph, `private_closed_mesh=0` means it did NOT earn R06 certification.

### Inspection colors
- Real successfully constructed cap surfaces stay green; zippers stay blue.
- ALL actual new cap and zipper triangulation edges are dark and drawn even
  when ordinary mesh wireframe is off; these edges are geometrically derived
  from the actual private mesh, not projections or approximated contour lines.
- Every face whose post-refinement quality `q` is below its existing floor
  (0.10 for a new cap or zipper; 0.012 for any bisected original survivor),
  or whose new-face edge length exceeds the existing hard target, is light
  red. All THREE edges of every such face are bright red, even if they are
  shared with other faces. These red features indicate violations, not
  that the classifier chose the wrong cap/zipper operation.
- Zero-area faces have no visible triangle fill but still contribute every
  nonzero edge to the red overlay.
- An untrusted staged result shows `R06: QUALITY WARN PREVIEW` at top right;
  the `AUTO_REFUSED` diagnostic is retained in the console and
  `QUALITY_WARNING_PREVIEW untrusted=1 private_closed_mesh=0` is logged.

### Non-quality safety gates
The warning-only path is intentionally **not** a new geometric repair
strategy. It may stop when no safe complete mirror-orbit split remains or
when the finite refinement budget is exhausted, then collect ALL remaining
quality/edge violations. It does not enlarge budget caps, perform partial
reflected edits, override R03 cut provenance, or create any worker history
or checkpoint transition. Topological manifold validation, post-refinement
triangle intersection validation, and structural staging failures are still
fatal: if one fails, `QUALITY_WARNING_UNAVAILABLE` explains why, and
the UI continues to show the real R03 cut. A structurally invalid patch
must not masquerade as a quality-only preview.

### Physical acceptance
Use the same archived case first with and then without warning mode.
In both runs check exact M03 area/proximity labels, expected strict
AUTO_REFUSED outcome, and lack of changes to original checkpoint/history.
On a warning-preview-capable case, inspect both sides and toggle W: the
real new edge lines remain visible, bad faces are salmon/pale red with
bright red enclosing edges, and the top-right label reports an untrusted
warning preview. On a normal success (e.g. physical case #2/#4), all
green/blue triangulation edges remain visible with no warning surface.
Test Linux and Windows Vulkan separately after CPU CI.

The captured V1 cases are immutable regression evidence. A preview never
changes their recorded or reported CLOSED/REFUSED outcome.
