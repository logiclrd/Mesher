#!/usr/bin/env python3
"""GPU-free tests for the macOS release bundle's dylib safety policy."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("package_macos.py")
spec = importlib.util.spec_from_file_location("package_macos", SCRIPT)
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class MacOSBundleTests(unittest.TestCase):
    def test_system_dylibs_are_not_copied(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Mesher"
            exe.touch()
            self.assertIsNone(bundle.find_library(
                "/usr/lib/libc++.1.dylib", exe, exe, [Path(tmp)]))
            self.assertIsNone(bundle.find_library(
                "/System/Library/Frameworks/Metal.framework/Metal",
                exe, exe, [Path(tmp)]))

    def test_resolve_loader_rpath_and_absolute_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            exe = base / "Contents" / "MacOS" / "Mesher"
            exe.parent.mkdir(parents=True)
            exe.touch()
            source = base / "source" / "libglfw.3.dylib"
            source.parent.mkdir()
            source.touch()
            prefix_lib = base / "brew" / "lib"
            prefix_lib.mkdir(parents=True)
            candidate = prefix_lib / "libqhull_r.8.dylib"
            candidate.touch()
            self.assertEqual(bundle.find_library(
                "@rpath/libqhull_r.8.dylib", source, exe,
                [base / "brew"]), candidate)
            self.assertEqual(bundle.find_library(
                "@loader_path/libglfw.3.dylib", source, exe, []), source)
            self.assertEqual(bundle.find_library(
                str(candidate), exe, exe, []), candidate)

    def test_missing_non_system_library_fails_instead_of_shipping_broken_app(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Mesher"
            exe.touch()
            with self.assertRaisesRegex(RuntimeError, "Unable to resolve"):
                bundle.find_library("@rpath/libMissing.dylib", exe, exe, [])
            with self.assertRaisesRegex(RuntimeError, "Missing linked"):
                bundle.find_library(str(Path(tmp) / "absent.dylib"),
                                    exe, exe, [])


if __name__ == "__main__":
    unittest.main()
