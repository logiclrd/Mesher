# V17-M73 — Default-on desktop sculpt mitigations (CLI only)

The user requested a command-line policy change, not a core
sculpting implementation change. Ordinary desktop sessions and
the animated `--demo` now start with **directional collapse,
curvature correction, and stretch-driven refinement enabled**.
The former `--collapse`, `--curvature-correction`, and
`--refine` options are removed from the CLI parser.
Disable individually with `--no-collapse`,
`--no-curvature-correction`, and `--no-refine`.

All numeric parameters and their defaults remain unchanged.
`--curvature-strength GAIN` now only configures strength:
the explicitly disabled `--no-curvature-correction` must not
be re-enabled by numeric arguments in either order.
`--no-edge-flips` still disables only the flip subpass.

Independent `--compare-remesh`, `--compare-mitigations`, and
`--compare-local-remesh` explicitly assign enable flags per
controlled variant and retain their baseline/experimental
semantics. Library-level settings defaults are unchanged.
The protected `--merge-geometry` switch stays opt-in/OFF;
checkpoint binary schema, runtime history and provenance
remain untouched. The M72 request for persisted runtime
settings is deferred.

Vulkan-enabled GCC/Clang/Windows MSVC CI must verify
default and disabled parser paths, old-flag rejection,
the `--curvature-strength` case, and all existing Merge
safe refusal, CTests, history stress and benchmarks.
On the user host, smoke test the default session and
all-three-disabled comparison in a fresh viewport.
Actual physical Windows/Vulkan M52 acceptance remains open.
