#!/usr/bin/env python3
"""Measure reversible, uncached edits in an already built disposable worktree."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import tempfile
import time


def replace_file(path, content):
    mode = path.stat().st_mode
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(content)
    temporary.chmod(mode)
    temporary.replace(path)


def command_output(command, cwd):
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def build_sample(args, target, name, sample):
    command = ["cmake", "--build", str(args.build), "--parallel", str(args.jobs),
               "--target", target, "--verbose"]
    transcript = args.output / f"{name}-{sample}-{target}.log"
    load_before = os.getloadavg()
    started = time.perf_counter()
    with transcript.open("w") as output:
        output.write(shlex.join(command) + "\n")
        output.flush()
        result = subprocess.run(command, cwd=args.source, stdout=output,
                                stderr=subprocess.STDOUT,
                                env={**os.environ, "CCACHE_DISABLE": "1"})
    elapsed = time.perf_counter() - started
    commands = [line for line in transcript.read_text().splitlines()
                if re.match(r"^\[\d+/\d+\] ", line)]
    compiles = [line for line in commands if " -c " in line]
    links = [line for line in commands if " -o " in line and " -c " not in line]
    objects = []
    for line in compiles:
        tokens = shlex.split(line)
        objects.append(tokens[tokens.index("-o") + 1])
    record = {"case": name, "sample": sample, "target": target,
              "seconds": elapsed, "compile_count": len(compiles),
              "link_count": len(links), "command_count": len(commands),
              "objects": objects, "load_before": load_before,
              "load_after": os.getloadavg(), "exit_code": result.returncode,
              "transcript": str(transcript)}
    print(f"{name} {sample} {target}: {elapsed:.3f}s, "
          f"{len(compiles)} compiles, {len(links)} links", flush=True)
    if result.returncode:
        raise RuntimeError(f"Build failed; inspect {transcript}")
    return record


def summarize(records):
    groups = {}
    for record in records:
        key = (record["case"], record["target"])
        groups.setdefault(key, []).append(record)
    return [{"case": case, "target": target,
             "median_seconds": statistics.median(row["seconds"] for row in rows),
             "minimum_seconds": min(row["seconds"] for row in rows),
             "maximum_seconds": max(row["seconds"] for row in rows),
             "compile_counts": [row["compile_count"] for row in rows],
             "link_counts": [row["link_count"] for row in rows]}
            for (case, target), rows in groups.items()]


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--targets", nargs="+", default=[
        "wormedit", "wormedit_frame_timing_tests"])
    parser.add_argument("--bgm-header", default="src/ui/frontend/compose.h")
    parser.add_argument("--cases", nargs="+", choices=[
        "no-op", "producer", "bgm", "root", "private"],
        default=["no-op", "producer", "bgm", "root", "private"])
    args = parser.parse_args()
    args.source = args.source.resolve()
    args.build = args.build.resolve()
    args.output = args.output.resolve()
    if args.jobs < 1 or args.samples < 3:
        parser.error("Use a positive job count and at least three samples")
    if not (args.source / ".git").is_file():
        parser.error("--source must be a disposable Git worktree")
    if args.output.exists():
        parser.error("--output must be a new directory, preserving earlier receipts")
    cache = (args.build / "CMakeCache.txt").read_text()
    for launcher in re.findall(r"^CMAKE_(?:C|CXX)_COMPILER_LAUNCHER:[^=]+=(.*)$",
                               cache, re.MULTILINE):
        if launcher:
            parser.error("Configure this build without compiler launchers")
    return args


def main():
    args = parse_arguments()
    args.output.mkdir(parents=True)
    probes = {"producer": "src/sys/compose/views/compose_marker_view.cpp",
              "bgm": args.bgm_header, "root": "src/ui/frontend/workspace.h",
              "private": "src/sys/project/workspace_view.h"}
    original = {name: (args.source / path).read_bytes()
                for name, path in probes.items() if name in args.cases}
    metadata = {"label": args.label, "source": str(args.source),
                "build": str(args.build), "jobs": args.jobs, "samples": args.samples,
                "targets": args.targets, "cases": args.cases, "probes": probes,
                "commit": command_output(["git", "rev-parse", "HEAD"], args.source),
                "cmake": command_output(["cmake", "--version"], args.source),
                "ninja": command_output(["ninja", "--version"], args.source),
                "compiler": command_output(["c++", "--version"], args.source),
                "cpu": command_output(["lscpu"], args.source),
                "cache_policy": "Compiler launchers empty; CCACHE_DISABLE=1; zero cache hits",
                "probe_sha256": {name: hashlib.sha256(data).hexdigest()
                                 for name, data in original.items()},
                "records": []}
    cache = (args.build / "CMakeCache.txt").read_text()
    metadata["configuration"] = [line for line in cache.splitlines()
                                 if re.match(r"^(CMAKE_(BUILD_TYPE|CXX_COMPILER|CXX_FLAGS|C_COMPILER|C_FLAGS)|WORMEDIT_)", line)]
    report = args.output / "measurements.json"

    def save():
        metadata["summary"] = summarize(metadata["records"])
        report.write_text(json.dumps(metadata, indent=2) + "\n")

    save()
    try:
        for target in args.targets:
            preflight = build_sample(args, target, "preflight", 0)
            if preflight["compile_count"] or preflight["link_count"]:
                raise RuntimeError("Prebuild both targets before measuring retained artifacts")
        for name in args.cases:
            for sample in range(1, args.samples + 1):
                if name != "no-op":
                    probe = f'\nstatic_assert(true, "t750 {name} sample {sample}");\n'.encode()
                    replace_file(args.source / probes[name], original[name] + probe)
                for target in args.targets:
                    metadata["records"].append(build_sample(args, target, name, sample))
                    save()
    finally:
        for name, content in original.items():
            replace_file(args.source / probes[name], content)
        save()
    # Restore build artifacts to the original contents as well; this is not a timed sample.
    for target in args.targets:
        build_sample(args, target, "restore", 0)
    print(f"Measurements: {report}", flush=True)


if __name__ == "__main__":
    main()
