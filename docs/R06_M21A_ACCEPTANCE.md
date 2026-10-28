# R06-M21A — Opportunistic whole-cap fairing, initial CPU acceptance

**Effective source basis:** all M20 geometry/diagnostic code and tests reverted in `b92c6a89986e839409df8a571cde472f981bee5b` using the exact pre-M20 M19A6 tree `02ab4a9a34de45f44457c8904af46da70f3e5833`. M20 ideas remain recoverable only in Git history. New M21 implementations are independent; the real R03 survivor cap rim and source edits are unchanged.

**Implementation tested:** `6068d80d5cfe55c863db143e041c0faf0da0ef7f`.
**Successful cross-platform workflow:** [#40227756478](https://github.com/logiclrd/Mesher/actions/runs/40227756478), Linux GCC, Linux Clang, Windows MSVC `/W4 /WX`: **129/129 CTest cases each**. A later two-line change only converts printed literal `\\n` into real log newlines; no algorithm changes.

## User acceptance contract

A **previously accepted original cap is the baseline**, not a defect to be removed. The new opt-in M21A local-copy optimizer evaluates full CAP/CAP and source rim dihedral continuity. It may replace the baseline positions only after a strictly better whole-cap score, nonregressing worst cap/cap and cap/survivor dihedral, minimum q>=0.10, fixed exact original R03 rim, orientation, bounded movement, mirror pairing and local cap-contact preflight. Failure, budget exhaustion before improvement, or insufficient improvement means **bitwise original cap** as fallback. An originally invalid cap is not granted acceptance by a baseline fallback.

This first implementation moves interior positions only; triangulation remains unchanged. Metric uses mean squared CAP/CAP normal jump + half mean squared original-survivor seam jump, with a small move-penalty on candidate rankings and worst-angle no-regression. No claim this is a faithful higher-order source-curvature tensor or intentional crease classifier. M21B/C can later improve broad shape retention and topology.

## Evidence

Unit fixtures: planar healthy cap gives **no change**; synthetic localized corrugation improves substantially; exact reflection orbits and original rim immutable; deterministic repetition and finite budgets; fully rejected validation callback and exhausted contact budget both return original cap unchanged. Experimental flag invalid outside interactive archived `--repair-step-debug` replay.

- Synthetic unsymmetric: score **0.136694 -> 0.00351917**, 3 rounds.
- Synthetic mirrored: score **0.505397 -> 0.00978397**, 3 rounds.
- Exact archived `repair-cases/refusal-1.r06case` with M17/M18 and provisional M19A6 position preview: `[repair-r06-m21a-fairing]` reports `candidate_applied=1`, `trials=61`, `rounds=3`, score **0.332947155 -> 0.321503416**, worst interior **162.227° -> 153.135°**, worst original-survivor seam **52.9699° -> 52.9699°**, minimum triangle q **0.117503**, exact original contour unchanged. The finite cap-collision budget was exhausted *after* admitted improvements; no further proposals were tried. Strict CPU test verifies no improvement / invalid proposals never replace the original.

**Critical limit:** these results demonstrate only candidate cap-local preflight plus the unchanged downstream PRIVATE source-backed validator. They do **not** show that the whole survivor+cap final closure result was accepted under the new preview flag. A source-complete retry-fallback transaction (if an improved cap fails whole-mesh validation) is REQUIRED before the user policy can be extended to any production/default publication. Do not interpret a successful local score as whole-mesh certification. Keep final GPU/whole-survivor acceptance and M21 production policy OPEN.

## Physical A/B command

```bash
build/Mesher --repair-replay repair-cases/refusal-1.r06case \
  --repair-step-debug --repair-scale-aware \
  --repair-front-optimize --repair-curvature-optimize \
  --repair-curvature-poisson --repair-feasibility-preview \
  --repair-whole-cap-fairing-preview --repair-seam-audit \
  --repair-overlay-exact
```

For the control remove only `--repair-whole-cap-fairing-preview`. Compare the generated cap around the same named `ENSUREFACE` steps and viewpoint, not a guessed step number. The log reports the baseline and selected whole-cap objective, worst cap/cap and original seam dihedral, and `baseline_fallback=1` when no improvement was admitted. Record mirrored side and complete downstream private closure verdict. User visual inspection remains outstanding.
