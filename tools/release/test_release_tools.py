#!/usr/bin/env python3
"""Tests desirable release invariants, not current failure modes."""
import importlib.util
import tempfile
import unittest
import subprocess
from unittest.mock import patch
from pathlib import Path

MODULE = Path(__file__).resolve().parent / "release_tools.py"
spec = importlib.util.spec_from_file_location("release_tools", MODULE)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseChecks(unittest.TestCase):
    def test_version_is_single_safe_label(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "VERSION"
            for value in ("0.5a\n", "1.2.3\n", "2.0rc1\n"):
                file.write_text(value, encoding="utf-8")
                self.assertEqual(release.read_version(file), value.strip())
            for invalid in ("", "0.5a\n0.6a\n", "0.5a/other\n",
                            "0.5a \n", "v0.5a\n", "0.5a;true\n"):
                file.write_text(invalid, encoding="utf-8")
                with self.assertRaises(ValueError):
                    release.read_version(file)

    def test_existing_tag_fails_closed_before_any_release_build(self):
        # Lightweight and annotated refs both appear as successful ls-remote
        # lookups. A network/auth error must never be mistaken for no tag.
        def probe(exit_code):
            return subprocess.CompletedProcess([], exit_code,
                                               stdout="", stderr="network error")
        with patch.object(release.subprocess, "run", return_value=probe(0)):
            with self.assertRaisesRegex(ValueError, "already exists"):
                release.check_tag("release-0.5a")
        with patch.object(release.subprocess, "run", return_value=probe(2)):
            release.check_tag("release-0.5a")
        with patch.object(release.subprocess, "run", return_value=probe(128)):
            with self.assertRaises(RuntimeError):
                release.check_tag("release-0.5a")

    def test_benchmark_requires_qhull_and_numeric_metrics(self):
        csv = ("benchmark,vertices,relaxation_iterations,generation_s,"
               "2_stamps_s,2_stamps_remesh_s\n"
               "mesh,25000,20,2.0,0.2,0.8\n")
        self.assertEqual(release.parse_benchmark(csv, "hull=Qhull")["generation_s"], 2)
        with self.assertRaises(ValueError):
            release.parse_benchmark(csv, "hull=quadratic")
        with self.assertRaises(ValueError):
            release.parse_benchmark(csv.replace(",0.8", ",nan"), "hull=Qhull")

    def test_prefers_faster_remeshing_compiler(self):
        gcc = [{"generation_s": 1.0, "2_stamps_s": 1.0, "2_stamps_remesh_s": 1.0}] * 5
        clang = [{"generation_s": 1.15, "2_stamps_s": 1.1, "2_stamps_remesh_s": 0.75}] * 5
        winner, _, scores = release.evaluate(gcc, clang)
        self.assertEqual(winner, "clang")
        self.assertLess(scores["clang"], scores["gcc"])
        winner, _, _ = release.evaluate(gcc, gcc)
        self.assertEqual(winner, "gcc")

    def test_medians_reject_outliers(self):
        slow = {"generation_s": 1., "2_stamps_s": 1., "2_stamps_remesh_s": 1.}
        fast = {"generation_s": .9, "2_stamps_s": .9, "2_stamps_remesh_s": .9}
        spikes = [{"generation_s": 999., "2_stamps_s": 999.,
                   "2_stamps_remesh_s": 999.}, fast, fast, fast, fast]
        winner, medians, _ = release.evaluate([slow] * 5, spikes)
        self.assertEqual(winner, "clang")
        self.assertEqual(medians["clang"]["2_stamps_remesh_s"], .9)


if __name__ == "__main__":
    unittest.main()
