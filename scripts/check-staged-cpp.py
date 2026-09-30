#!/usr/bin/env python3
"""Syntax-check the Git index in a temporary snapshot using existing compiler commands.

Generated headers and dependencies stay in the existing build directory. Repository
src/include/tests paths resolve to the index snapshot, never unstaged source files.
Sources, the index, and builds are unchanged; only an explicitly requested log is written.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def compiler_arguments(entry, repo, snapshot, source):
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    result = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
            continue
        if argument in {"-o", "-MF", "-MT", "-MQ", "-MJ"}:
            skip = True
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        if argument.startswith(("-o", "-MF", "-MT", "-MQ", "-MJ", "-Wl,")):
            continue
        if not argument.startswith("-"):
            candidate = (Path(entry["directory"]) / argument).resolve()
            if candidate.is_relative_to(repo) and candidate.relative_to(repo).parts[:1] in [("src",), ("include",), ("tests",)]:
                argument = str(snapshot / candidate.relative_to(repo))
        for prefix in ("-I", "-iquote", "-isystem"):
            if argument.startswith(prefix) and argument != prefix:
                candidate = (Path(entry["directory"]) / argument[len(prefix):]).resolve()
                if candidate.is_relative_to(repo) and candidate.relative_to(repo).parts[:1] in [("src",), ("include",), ("tests",)]:
                    argument = prefix + str(snapshot / candidate.relative_to(repo))
        for tree in ("src", "include", "tests"):
            argument = argument.replace(str(repo / tree) + os.sep, str(snapshot / tree) + os.sep)
            if argument.endswith(str(repo / tree)):
                argument = argument[:-len(str(repo / tree))] + str(snapshot / tree)
        if argument == entry["file"] or argument == str(source):
            argument = str(snapshot / source.relative_to(repo))
        result.append(argument)
    result.append("-fsyntax-only")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sources", nargs="*", help="Repository-relative sources; default: staged added/modified .cpp files")
    parser.add_argument("--compile-db", type=Path, default=Path("build/compile_commands.json"), help="Compilation database file or directory")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--log", type=Path, help="Optional compiler output log")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        repo = Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip())
        database = args.compile_db / "compile_commands.json" if args.compile_db.is_dir() else args.compile_db
        entries = json.loads(database.read_text())
        names = args.sources or subprocess.check_output(["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"], cwd=repo).decode().split("\0")
        sources = [(repo / name).resolve() for name in names if name and (args.sources or name.endswith(".cpp"))]
        for source in sources:
            if not source.is_relative_to(repo):
                raise ValueError(f"source outside repository: {source}")
        if not sources:
            print("No staged C++ sources to check")
            return 0
        commands = {}
        for entry in entries:
            directory = Path(entry["directory"])
            source = Path(entry["file"])
            commands[(directory / source).resolve()] = entry
        missing = [str(source.relative_to(repo)) for source in sources if source not in commands]
        if missing:
            raise ValueError("no compiler command for: " + ", ".join(missing))
        with tempfile.TemporaryDirectory(prefix="staged-cpp-") as temporary:
            snapshot = Path(temporary)
            subprocess.run(["git", "checkout-index", "--all", f"--prefix={snapshot}{os.sep}"], cwd=repo, check=True)
            def check(source):
                if not (snapshot / source.relative_to(repo)).is_file():
                    return str(source.relative_to(repo)), 2, "Source is absent from the Git index\n"
                entry = commands[source]
                result = subprocess.run(compiler_arguments(entry, repo, snapshot, source), cwd=entry["directory"], capture_output=True, text=True)
                return str(source.relative_to(repo)), result.returncode, result.stdout + result.stderr
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(check, sources))
        report = []
        for name, status, output in results:
            print(f"{'PASS' if status == 0 else 'FAIL'} {name}")
            report.append(f"{name}: exit {status}\n{output}")
            if status and not args.log:
                print(output, end="")
        if args.log:
            args.log.write_text("\n".join(report))
        return int(any(status for _, status, _ in results))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
