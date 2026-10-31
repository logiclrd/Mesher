# UX07F — End-to-end export / private re-import acceptance

**Status:** Automation on [draft PR #192](https://github.com/logiclrd/Mesher/pull/192). Final acceptance and the [active UX07F TODO](../todo.md) remain open until latest exact-head CI and a physical native-picker smoke test are reviewed. These tests do **not** certify that a Windows or macOS native dialog was exercised on hardware.

## Source of truth and test boundaries

The concrete production path under test is:

`RegionTopologyIndex` accepted indexed root → `captureMeshExportRequest`
→ `meshExportEncoder(selected preset)` → `AsyncMeshExportWriter`
→ unique adjacent temporary file + sync + atomic replacement
→ `readMeshOpenCandidate(path, MeshOpenFilter::AllFiles)`
→ a privately admitted immutable imported revision.

The export is **never a checkpoint Save**: it neither commits the editing history nor alters a dirty document's checkpoint destination. Dedicated UX07D tests already exercise stalled I/O, CWD changes, concurrent publication, queue order and injected failures; UX07F reuses that assurance instead of making duplicate performance or UI tests.

All **21 presets** are exercised against a nonwhite, variable RGBA tetrahedron with separately varying source-alpha metadata:

| Encoding group | Number | Expected outcome |
| --- | ---: | --- |
| OBJ standard | 1 | Positions and oriented closed faces; RGB/alpha intentionally absent |
| OBJ inline RGB/RGBA, ZBrush MRGB | 3 | RGB fidelity, inline RGBA opacity, MRGB 8-bit quantization; mask byte is **not** alpha |
| PLY ASCII/LE/BE × geometry, RGB, mesh-alpha, source-alpha | 12 | Geometric fidelity, RGB quantization ≤0.5/255, correct explicit alpha source, private user-alpha decision before adopting PLY source alpha |
| STL ASCII/binary | 2 | Watertight oriented welded geometry, no RGB/alpha |
| STL VisCAM/Magics facet RGB | 2 | 5-bit facet means followed by UX06 facet→vertex diffusion; only bounded/plausible colour and geometry invariants, not a falsely exact vertex-RGB reconstruction |
| STL Magics object RGBA | 1 | A single 8-bit-quantized object-wide average, spatial colour variation deliberately lost |

STL can reorder/weld original vertices, so the matrix matches unique coordinates and oriented face cycles; it does **not** compare raw input and output vertex indices. Lossy output is tested against the documented conversion rather than against an impossible identity claim. The PLY original-alpha branch explicitly calls `applyPlySourceAlphaAsOpacity` and requires a distinct private indexed candidate before checking editable alpha.

For each successful export the test verifies the selected destination, one ordered completion, clean temporary-file removal, import-format auto-detection from **content**, complete editable closed topology, coordinate/winding and supported colour/alpha fidelity, and no change to the original immutable indexed source.

A failure case requests a PLY *original source alpha* export when one accepted vertex lacks source-alpha provenance. The **actual** encoder must reject it asynchronously, report a failed completion and preserve the pre-existing destination bytes. Existing UX07D tests cover empty/invalid paths, injected stream failures, missing directory, directory destination, 2 MiB buffered write, concurrent source publication/CWD changes and FIFO requests.

## Real-source regression fixtures

Same test executable (no additional linker targets) also exports and privately re-imports three repository fixtures: `test-files/Duck.ply` (PLY LE RGB), `Duck-ZBrush.obj` (ZBrush MRGB), and `castle.stl` (Magics facet RGB). These check complete topology/vertex counts and either representative sampled coordinate/RGB fidelity (indexed OBJ/PLY) or 32-bit-quantized geometric extrema (STL). Do not interpret re-import success as an independent measurement of every visual feature on the source model.

Run via the existing CTests:

```sh
cmake --build build -j "$(nproc)"
ctest --test-dir build -R 'mesh_export_native_dialog_codec_policy|ux07f_real_.*_export_reimport|mesh_export_atomic_snapshot_and_fifo|obj_export_formats_and_roundtrips|ply_export_encoding_colour_roundtrips|stl_export_dialect_and_colour_roundtrips' --output-on-failure
```

## Physical GUI acceptance — one shared QA-UX gate

On a full graphics workstation, with a currently edited and visibly painted mesh:

1. Use Ctrl+E and inspect native OS picker: all 21 selectable format/colour policies, appropriate loss warnings, supported suggested extensions and CWD start/accept behavior. Cancel and verify no file and no edits/dirty-state change.
2. Choose a preserving PLY RGBA mode and export. Ctrl+O the result; compare surface geometry, colours, saved opacity and camera framing. Use a separate source-alpha PLY export when source-alpha provenance exists. Confirm that the importer's explicit alpha interpretation behaves as disclosed.
3. Export both OBJ inline RGBA and ZBrush MRGB, reopen each, compare geometry and coloured regions; MRGB must lose alpha, not reinterpret the mask as opacity.
4. Export binary colourless STL and one facet-colour STL; reopen and confirm geometry. Expect averaged/dithered colour fidelity on coloured STL, not exact original vertex values. Check the loss warnings *before* acceptance.
5. Overwrite an existing foreign-format destination only after native confirmation. Exercise file-dialog cancellation and an inaccessible directory or other realistic I/O failure. Check that old bytes and the original editor/undo history survive and that the asynchronous error is shown.
6. While exporting, continue painting/sculpting and verify the export corresponds to the accepted snapshot at selection time, not the later edited root. Reopen a new document and confirm its native checkpoint Save identity is separate from the Ctrl+E export path.

Record each actual physical system **separately**: Linux/X11, Windows and macOS. Automated build/CTest success does not count as a Windows/macOS GUI observation. This physical matrix is shared with UX06 and QA-UX; do not create redundant verification items per codec, per format or per OS in the active TODO.

## Completion criteria

- The **exact current PR head** passes Linux GCC, Linux Clang, Windows MSVC, macOS arm64 and macOS x86_64 warnings-as-errors and relevant full CTests.
- The 21-preset end-to-end integration and three real-file cases pass on supported build environments.
- Physical Linux GUI smoke test is reviewed and accepted by the user; any Windows/macOS hardware gaps remain explicitly in QA-UX, not silently inferred from CI.
- Then mark UX07F complete and leave QA-UX as the canonical cross-platform follow-up.
