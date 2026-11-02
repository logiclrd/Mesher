# CI01 — Consolidate CPU regression test executables

## Baseline (measured, not estimated)

The most recent successfully completed Windows/MSVC five-platform CI run before
the CI01 change is [run 41286339520](https://github.com/logiclrd/Mesher/actions/runs/41286339520),
job 124788521063, on 2026-11-01.

- MSVC `cmake --build build --config Release --parallel 4` started at
  07:52:44.340 UTC; the subsequent tests step started at 07:57:34.143 UTC.
- **Total build step wall time: 289.8 seconds**, including compilation, shaders,
  static library, all applications/tests and linking. This is *not* exclusively
  time spent linking tests.
- The MSBuild log reports **97 linked .exe lines**. The original CMakeLists
  contains 99 `add_executable` declarations, including mutually exclusive
  platform-specific definitions of the same `Mesher` target. The linked-EXE
  count is the reproducible metric for this Windows CI configuration.
- The existing Windows run passed its CTest suite. Record the new run's
  linked-EXE count and wall time before claiming improvement. Windows runner
  hardware, vcpkg cache and scheduling may introduce run-to-run variance.

## CI01 first consolidation

Sixteen CPU-only, argument-free test translation units previously had sixteen
small separate executable projects. They are now built as a single
`mesher_ci01_cpu_regressions` target.

Each original test's `main()` is renamed *in that source file only* using a
per-file compile definition. The new dispatcher invokes exactly one renamed
entry point selected by its CTest name. **CTest still spawns a distinct process
for each named test**, preserving:

- All original CTest test names, `ctest -R` filtering, and full suite coverage
- Exit codes, individual stdout/stderr, working directory and CTest reporting
- Per-test timeouts and concurrent CTest scheduling (where applicable)
- The original test logic, exception handlers, data fixtures and seeds. Every
  grouped test now ends with an explicit `return 0;`: C++ only supplies this
  return implicitly to a function literally named `main`, not to its renamed
  entry point. This is semantics-preserving and avoids suppressing warnings.
- C++20 and warnings-as-errors on Linux GCC/Clang, Windows MSVC and macOS
  Apple Clang arm64/x86_64, without new runtime/tool dependencies

The sixteen grouped tests cover winding, repair-plan/closure/surgery,
revision retirement, spatial queries, vertex paint, mesh partitioning,
region editing, incremental index, versioned mesh/inflate/refine/collapse,
partition rendering and screen-space wire quads. The heavy standalone
repair replay, async/concurrency and executable-argument tests remain
separate to avoid altering test boundaries or argument contracts.

CMake `add_executable` declarations drop from 99 to 84 (15 fewer).
Expected Windows linked executables are 82 rather than 97, subject to the
same build configuration. No test registrations are deliberately removed.

## Second consolidation: four bounded linked suites

The first CI01 implementation passed all five platform configurations in
[run 41288877898](https://github.com/logiclrd/Mesher/actions/runs/41288877898).
However, MSVC still linked **82 executables**, and the total Windows build step
increased to **334.5 seconds** (from the 289.8-second baseline). Reducing
a small number of link steps therefore did **not** show a speed improvement.

The follow-up consolidates **68 additional one-mode, mesher_mesh-only** C++
test targets into four independently linkable suites of 20, 18, 16, and
14 test translation units. The original first group of 16 remains separate.
CMake declarations drop further from 84 to **20**; the comparable Windows
build **does link 18 .exe files**, instead of 82. The exact-head
[run 41291446766](https://github.com/logiclrd/Mesher/actions/runs/41291446766)
passed all five platform build/test jobs and retained the **162-test** full
CTest suite. Windows MSVC built and linked the 18 executables in **373.7
seconds**, measured from the start of `cmake --build build --config Release
--parallel 4` to the start of the next CI step.

**Important:** This is a reduction of 79 executables relative to the
original 97, but it is *not* a speed improvement: 373.7 seconds is slower
than both the original 289.8-second and first-consolidation 334.5-second
runs. Consolidating TUs into larger Visual Studio projects appears to have
reduced target-level compilation parallelism and/or increased linker
bottlenecks. That is a hypothesis, not yet a profiling result.

All 68 tests still have their original individual CTest names, environments,
working directories, per-test timeout/fixtures and process isolation. Existing
test assertions and fixture logic are unchanged. Only entry points
that relied on the implicit success return of the literal C++ `main()` gain
an explicit `return 0;` (22 files); tests that already return on all paths
remain unchanged (46 files), avoiding MSVC C4702 unreachable-code warnings.
The same per-source main renaming and named dispatchers are used as above.
Four bounded link groups avoid relying on a single giant serial linker step.

**Deliberately excluded**: platform-specific Qt/GLFW desktop test runners,
tests with custom compile/link settings, multi-mode command-line/fixture
executables, and the standalone performance benchmark. Their special semantics
must not be silently changed merely to obtain a smaller .exe count.

A performance claim requires the new run's actual MSVC compile/link time,
number of linked .exe artifacts and all five warnings-as-errors/CTest suites.
Windows runner variance and link-versus-compilation costs should be recorded
before further expansion or closure of CI01. A reduced binary count by itself
does not demonstrate a faster pipeline. The next CI01 step should profile
MSBuild target-level and per-translation-unit concurrency, and benchmark
bounded `/MP` compilation or alternate test target grouping before claiming
build-speed improvement. Do not close CI01 solely on the executable count.

## Third measurement: opt-in bounded MSVC compilation

The exact-code baseline [run 41291446766](https://github.com/logiclrd/Mesher/actions/runs/41291446766)
passed all five platforms and all 162 CTests. Windows linked 18 executables,
but its complete `cmake --build --config Release --parallel 4` step took
**373.7 seconds**. The Windows job log shows `mesher_mesh.lib` finishing
roughly 226 seconds after the build began, while five consolidated test
executables finished over the remaining 148 seconds.

The third experiment adds the cache setting `MESHER_CI01_MSVC_MP`:

- `0` (default): preserve the original MSVC compilation settings. Ordinary
  developer builds remain unchanged unless explicitly opted in.
- `2`: add `/MP2` to `mesher_mesh` and the five bundled CPU test targets.
- `4`: allow a follow-up `/MP4` benchmark if the first result warrants it.

First measurement: [CI 41366953831](https://github.com/logiclrd/Mesher/actions/runs/41366953831)
configured `-DMESHER_CI01_MSVC_MP=2` and retained `--parallel 4`.
All five platforms and all 162 Windows CTests passed, with 18 linked
Windows executables. On the four-logical-processor runner, the measured
build step fell to **308.9 seconds** (17.3% faster than the 373.7-second
consolidated no-/MP build), but still lagged the original 289.8-second
97-executable baseline. The `mesher_mesh.lib` link finished 160.4 seconds
into the build instead of about 226 seconds; the remaining build tail did
not materially improve. The MSBuild performance summary recorded 103 CL
task calls and 18 linker calls (sum of task durations is *not* wall time).

Second measurement in progress: CI now configures
`-DMESHER_CI01_MSVC_MP=4` with `--parallel 2`. This retains an upper
bound of eight concurrent compiler processes (2 MSBuild projects times
4 per-target compiler processes), while prioritising parallel compilation
inside the heavy mesh library. The build continues to print effective
processors and `/clp:PerformanceSummary` profiling. Actual Windows
results must be recorded before selecting the preferred setting.
No non-MSVC compiler flags, test logic, CTest registration or output-format
behaviour changes.

Acceptance requires all five platforms green, the same 18 Windows executable
links, the original 162 tests and a measured Windows build-step duration
compared against the 289.8, 334.5 and 373.7 second historical runs.
Because hosted runners vary, one build alone is directional evidence, not a
statistically robust speed claim. Subsequent runs can compare /MP4 and
different MSBuild project caps without altering the sources.

## Verification protocol

1. Confirm the grouped sixteen names still appear once in
   `ctest -N` and remain independently selectable via `ctest -R`.
2. Check all sixteen grouped tests pass on GCC, Clang, MSVC and Apple Clang,
   and the complete suites pass with unchanged total CTest test counts.
3. Check that a renamed entry-point dispatcher reports failure for an
   unrecognized test name, rather than silently succeeding.
4. Compare exact-head CI linked executable counts and Windows
   `cmake --build` step wall times with the baseline above. Interpret small
   build-time differences cautiously.
5. If the first group gives useful results, consider additional *independent*
   groups instead of turning every test into a single long serial executable.

The active TODO retains CI01 until the measured outcome is reviewed and the
remaining larger test-target count is explicitly accepted or reduced further.
