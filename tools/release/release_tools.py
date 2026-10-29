#!/usr/bin/env python3
"""Release preflight and repeatable same-runner Linux compiler comparison.

Only stdlib dependencies; GitHub Actions invokes this before any release build.
"""
import argparse
import csv
import io
import json
import math
import re
import statistics
import subprocess
import sys
from pathlib import Path

VERSION_PATTERN = re.compile(r"[0-9]+(?:\.[0-9]+)+(?:[A-Za-z][A-Za-z0-9.-]*)?\Z")
METRICS = ("generation_s", "2_stamps_s", "2_stamps_remesh_s")
WEIGHTS = (0.10, 0.10, 0.80)


def read_version(path):
    content = Path(path).read_text(encoding="utf-8")
    if len(content.splitlines()) != 1:
        raise ValueError("VERSION must contain exactly one line")
    version = content.removesuffix("\n").removesuffix("\r")
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError("Invalid VERSION; expected e.g. 0.5a or 1.2.3")
    return version


def check_tag(tag):
    # An existing annotated or lightweight tag must fail, not be overwritten.
    # Exit 2 means a reachable remote with no matching ref. All other errors
    # (including network/auth failure) are fatal, rather than assumed absent.
    result = subprocess.run(
        ["git", "ls-remote", "--exit-code", "--refs", "origin", f"refs/tags/{tag}"],
        text=True, capture_output=True, check=False,
    )
    if result.returncode == 0:
        raise ValueError(f"Refusing release: tag {tag} already exists on origin")
    if result.returncode != 2:
        raise RuntimeError(f"Could not inspect remote tags: {result.stderr.strip()}")


def append_output(path, **items):
    if path:
        with Path(path).open("a", encoding="utf-8") as output:
            for name, value in items.items():
                output.write(f"{name}={value}\n")


def parse_benchmark(stdout, stderr):
    if "hull=Qhull" not in stderr:
        raise ValueError("Benchmark is not using Qhull")
    lines = [line for line in stdout.splitlines() if line.startswith("benchmark,") or line.startswith("mesh,")]
    if len(lines) != 2:
        raise ValueError(f"Unexpected benchmark CSV: {stdout[-500:]}")
    row = next(csv.DictReader(io.StringIO("\n".join(lines))))
    result = {}
    for key in METRICS:
        value = float(row[key])
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"Benchmark metric {key} must be finite and positive")
        result[key] = value
    return result


def evaluate(gcc, clang):
    """Compare normalized medians; 80% weight on latency of remeshing stamps."""
    if not gcc or not clang:
        raise ValueError("Both compilers need measured results")
    medians = {name: {metric: statistics.median(row[metric] for row in samples)
                      for metric in METRICS}
               for name, samples in (("gcc", gcc), ("clang", clang))}
    baseline = medians["gcc"]
    scores = {name: sum(weight * data[metric] / baseline[metric]
                        for metric, weight in zip(METRICS, WEIGHTS))
              for name, data in medians.items()}
    # Exact tie goes to GCC; any repeatable lower median score selects Clang.
    winner = "clang" if scores["clang"] < scores["gcc"] else "gcc"
    return winner, medians, scores


def select(args):
    programs = {"gcc": args.gcc, "clang": args.clang}
    samples = {"gcc": [], "clang": []}
    # All timings from one physical CI runner and its two compiler builds.
    # Warm both caches, then alternate AB / BA to limit order bias.
    for round_index in range(-1, args.rounds):
        order = ("gcc", "clang") if round_index % 2 == 0 else ("clang", "gcc")
        for compiler in order:
            command = [str(Path(programs[compiler]).resolve()),
                       str(args.vertices), str(args.relax)]
            completed = subprocess.run(command, text=True, capture_output=True,
                                       check=True, timeout=args.timeout)
            measurement = parse_benchmark(completed.stdout, completed.stderr)
            print(f"{compiler} round={round_index} " +
                  " ".join(f"{key}={value:.5f}" for key, value in measurement.items()),
                  flush=True)
            if round_index >= 0:
                samples[compiler].append(measurement)
    winner, medians, scores = evaluate(samples["gcc"], samples["clang"])
    print(f"Linux compiler selected: {winner} (GCC score={scores['gcc']:.5f}; "
          f"Clang score={scores['clang']:.5f}; lower is better)")
    result = {
        "selected": winner,
        "method": "one Ubuntu runner, alternating order, medians, " +
                  "normalized weighted score (10% generation, 10% plain stamp, 80% remesh stamp)",
        "rounds": args.rounds,
        "vertices": args.vertices,
        "relaxation_iterations": args.relax,
        "medians_seconds": medians,
        "scores": scores,
        "samples_seconds": samples,
    }
    Path(args.report).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    append_output(args.github_output, compiler=winner)
    if args.github_summary:
        with Path(args.github_summary).open("a", encoding="utf-8") as summary:
            summary.write(f"### Linux compiler selection: {winner}\n\n")
            summary.write("| Compiler | Generation s | Plain stamps s | Remesh stamps s | Score |\n")
            summary.write("|---|---:|---:|---:|---:|\n")
            for name in ("gcc", "clang"):
                m = medians[name]
                summary.write(f"| {name} | {m['generation_s']:.5f} | "
                              f"{m['2_stamps_s']:.5f} | "
                              f"{m['2_stamps_remesh_s']:.5f} | {scores[name]:.5f} |\n")
            summary.write("\nMeasured on the same runner; lower normalized score wins.\n")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    preflight = commands.add_parser("preflight")
    preflight.add_argument("--version-file", default="VERSION")
    preflight.add_argument("--github-output")
    # PRs validate packaging without claiming the current VERSION as a new
    # release. Real push/manual publications still require unused tags.
    validate = commands.add_parser("validate")
    validate.add_argument("--version-file", default="VERSION")
    validate.add_argument("--github-output")
    check = commands.add_parser("check-tag")
    check.add_argument("--tag", required=True)
    selection = commands.add_parser("select")
    selection.add_argument("--gcc", required=True)
    selection.add_argument("--clang", required=True)
    selection.add_argument("--rounds", type=int, default=5)
    selection.add_argument("--vertices", type=int, default=25000)
    selection.add_argument("--relax", type=int, default=20)
    selection.add_argument("--timeout", type=int, default=180)
    selection.add_argument("--report", required=True)
    selection.add_argument("--github-output")
    selection.add_argument("--github-summary")
    args = parser.parse_args()
    if args.command == "preflight":
        version = read_version(args.version_file)
        tag = f"release-{version}"
        check_tag(tag)
        append_output(args.github_output, version=version, tag=tag)
        print(f"Preflight passed: version={version}, available tag={tag}")
    elif args.command == "validate":
        version = read_version(args.version_file)
        tag = f"release-{version}"
        append_output(args.github_output, version=version, tag=tag)
        print(f"PR packaging validation for {version} (no tag created)")
    elif args.command == "check-tag":
        check_tag(args.tag)
        print(f"Tag is still available: {args.tag}")
    else:
        if args.rounds < 3 or args.vertices < 10000 or args.relax <= 0:
            parser.error("Comparison requires >=3 rounds, >=10000 vertices and positive relaxation")
        select(args)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, subprocess.SubprocessError, OSError) as error:
        print(f"Release check failed: {error}", file=sys.stderr)
        sys.exit(1)
