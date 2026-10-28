# R06 AFM retirement — production-only cap geometry

Status: Production cap-construction cleanup, 2026-10-27.

## Kept

- M22B advancing-front (AFM) cap generation, including evolving accepted-face
  normals, symmetry-plane constraints, finite work, collision and quality
  checks, and source-backed staging.
- M22A generic fixed-boundary Poisson solve over arbitrary indexed interior
  topology. Both ordinary caps and M23B joined zipper caps use it.
- M23B bridge, opposite-side attraction, and AFM zipper cap.
- M24 shape-driven conforming zipper refinements and hard admission limits.
- R03 excision, R06 planning and private certificate, R07 single-root
  publication, symmetric stable-ID journaling, undo/redo and checkpoint.
- The opt-in read-only step debugger, exact seam audit/overlay, saved-case
  replay, and accepted recovered-case positive regression tests.
- Optional M17C scale-aware downstream policy (not a ring-index optimizer)
  and independent projected-ear-clipping diagnostic path.

## Retired

The legacy inset ring + hub and mirrored-ribbon generators were not invoked
by the accepted M22B AFM route. The obsolete generator plus M17D inward-ring
overshoot search, M18D first-ring continuity fit, M18E first-ring curvature
Poisson, M19A/M19D hypothetical ring candidates and M21A ring fairing were
removed. The corresponding C++ experimental headers and retired tests are
available in Git history but no longer compiled.

The ring-specific CLI flags now fail explicitly rather than silently leaving
the accepted AFM geometry unchanged. The generic Poisson solver and its
independent tests must NOT be removed.

## Failure and regression policy

A rejected AFM candidate is refused: do not fall back to ring-and-hub or
publish half-closed geometry. Existing projected-ear clipping is a separate,
explicit opt-in FIRST attempt; when requested it may fall through to AFM,
not to ring-and-hub.

Preserve the five archived V1 cases without rewriting expected historical
records. The current passing tests require certified AUTO_CLOSED for the
reviewed cases, legal q/edge and manifold/contact checks, exact symmetry,
immutable original source, live worker continuation, undo and redo.
Do not encode historical low-quality ring failures as desired test output.
