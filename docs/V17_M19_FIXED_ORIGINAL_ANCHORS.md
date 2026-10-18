# V17-M19 — Fixed-original source-FaceId surface anchor

## Scope and guarantees

This peer implements the first read-only contract extracted from M12.
It deliberately does not yet activate a physical sculpt Step guard, enable
the desktop switch, supply split/flip/collapse lineage, or close M12.
Those obligations have explicit flat peers M20–M23 and M12 integration,
recorded in todo.md before implementation.

A captured anchor retains a shared immutable ORIGINAL index pointer,
a nominated original FaceId, its **ordered original VertexIds**,
seven original fixed XYZ witnesses (3 corners, 3 edge midpoints and
centroid), and the **shortest finite nonzero source edge length**.
This is a conservative original-face-local geometry scale, not a
brush radius or the previous revision's progressively drifting edge.

The bounded M19 evaluator first verifies that the caller still names
the exact captured original index (not a previous intermediate root).
Each living candidate FaceId must have the SAME ordered three original
VertexIds. An identical or nearby XYZ triangle with a different FaceId
cannot replace this owner. A source face that has been retired,
subdivided, or topologically rewritten is **refused** until M20 supplies
separately certified descendant provenance. M19 does not convert a
missing witness into a harmless unprotected patch, infer intended
deformation from deviation, or snap either side of a coincident sheet.

For every original source face, the evaluator measures its seven fixed
witnesses against ONLY its correspondingly identified living triangle.
It returns max/mean/RMS normalized by THAT ORIGINAL face's shortest
edge; aggregation is by witness count. A hard finite work budget is
checked before any distance work. With N nominated distinct faces,
the exact bound is 7*N distance tests: equality accepts; one below
throws length_error. Invalid, duplicated or missing source references,
nonfinite/degenerate scales and changed/retired source incidence refuse.
The audit itself is read-only and never publishes a mesh root.

## Independent acceptance

- Positive: a two-face closed-cube source captures 14 witnesses, proves
  zero initial deviation, exactly 14 distance tests, and an actual
  normal-driven Inflate displacement with positive drift from the SAME
  immutable first reference.
- Negative: work limit 13, capture cap 13, duplicate original FaceId,
  a different (stale/substituted) reference root, and a candidate with
  the original FaceIds reassigned to other original triangle incidences
  refuse; old root/picker/tombstones remain unchanged.
- Regression: existing V16 fidelity and contact tracker fixtures,
  M11 genuine sixteen physical exact unions, and M16 off-plane/on-plane
  coincident-ID safety retain their prior behavior because M19 has no
  physicalExact policy or UI activation.

**Deliberate limitation:** Fixed samples on unchanged original-ID
triangles are finite one-sided evidence only. A changed face topology
must refuse here even if an actually correct remesh occurred; M20 owns
proof-bearing ancestry of that remesh. This is neither a two-sided
Hausdorff certificate nor a positive Boolean/winding/cavity proof.
