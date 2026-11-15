#!/usr/bin/env python3
"""Ratchet: test source files stay small enough to compile and review in one piece.

A test .cpp/.h/.mm under tests/ may hold at most LIMIT lines. Files that were already larger when
this check was added are listed in tests/scripts/test_file_size_baseline.txt with their current
length: such a file may shrink, never grow, and a file missing from the baseline must stay at or
under LIMIT. Split a big file by topic instead of growing it; run with --update after a split so
the baseline follows the new, smaller length (a file that drops to LIMIT or less leaves the list).
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).with_name("test_file_size_baseline.txt")
LIMIT = 1500
SUFFIXES = {".cpp", ".h", ".mm"}


def count_lines() -> dict[str, int]:
    counts: dict[str, int] = {}
    for path in sorted((ROOT / "tests").rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        counts[path.relative_to(ROOT).as_posix()] = len(path.read_text(encoding="utf-8").splitlines())
    return counts


def read_baseline() -> dict[str, int]:
    baseline: dict[str, int] = {}
    if BASELINE.exists():
        for line in BASELINE.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                name, _, number = line.rpartition(" ")
                baseline[name] = int(number)
    return baseline


def main() -> int:
    counts = count_lines()
    if "--update" in sys.argv:
        lines = [f"# file line-count; files over {LIMIT} lines may only shrink (see check_test_file_size.py)"]
        lines += [f"{name} {number}" for name, number in counts.items() if number > LIMIT]
        BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 0
    baseline = read_baseline()
    failures = [f"{name}: {number} lines, allowed {max(LIMIT, baseline.get(name, 0))}; split it by topic "
                f"(see tests/sys/frame_timing_fixtures.h for how shared helpers move out)"
                for name, number in counts.items() if number > max(LIMIT, baseline.get(name, 0))]
    for line in failures:
        print(line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
