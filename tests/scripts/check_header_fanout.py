#!/usr/bin/env python3
"""Ratchet: a first-party header may not be reached by more headers than its baseline.

Rebuild cost follows header fan-out: touching a header recompiles every .cpp that includes it
directly or transitively. This builds the include graph of src/, include/ and tests/ from the
#include lines (no build needed; #if branches are all followed) and counts, per header, how many
OTHER HEADERS reach it. Counting headers rather than .cpp files means adding a new source file
never trips the check; only a new #include inside a header (which widens every includer) does.

- A header listed in tests/scripts/header_fanout_baseline.txt may not exceed its number.
- A header missing from the baseline may be reached by at most LIMIT headers. To add a wide
  header on purpose, split it, forward-declare, or edit the baseline in a reviewed change.
- Only headers reached by at least LIMIT headers are listed. Run with --update after a drop to
  lower the baseline; --top N prints the widest headers with their .cpp reach too.
"""
import pathlib
import re
import sys
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = pathlib.Path(__file__).with_name("header_fanout_baseline.txt")
LIMIT = 20
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
SEARCH_ROOTS = [ROOT / "src", ROOT / "include", ROOT / "tests"]


def sources() -> list[pathlib.Path]:
    found = []
    for base in SEARCH_ROOTS:
        found += [p for p in base.rglob("*") if p.suffix in (".cpp", ".h", ".hpp", ".inc")]
    return sorted(found)


def resolve(includer: pathlib.Path, kind: str, name: str) -> pathlib.Path | None:
    candidates = [includer.parent / name] if kind == '"' else []
    candidates += [base / name for base in SEARCH_ROOTS]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def build_graph() -> tuple[dict[pathlib.Path, set[pathlib.Path]], list[pathlib.Path]]:
    graph: dict[pathlib.Path, set[pathlib.Path]] = {}
    units = []
    for path in sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        edges = set()
        for kind, name in INCLUDE.findall(text):
            target = resolve(path, kind, name)
            if target is not None:
                edges.add(target)
        graph[path.resolve()] = edges
        if path.suffix == ".cpp":
            units.append(path.resolve())
    return graph, units


def fanout() -> tuple[dict[str, int], dict[str, int]]:
    graph, units = build_graph()
    reached: dict[pathlib.Path, set[pathlib.Path]] = defaultdict(set)
    for unit in graph:
        seen: set[pathlib.Path] = set()
        stack = [unit]
        while stack:
            for dep in graph.get(stack.pop(), ()):
                if dep not in seen:
                    seen.add(dep)
                    stack.append(dep)
        for header in seen:
            reached[header].add(unit)
    unit_set = set(units)
    headers = {h.relative_to(ROOT).as_posix(): len(u - unit_set) for h, u in reached.items()
               if h.suffix != ".cpp" and len(u - unit_set) >= LIMIT}
    cpps = {h.relative_to(ROOT).as_posix(): len(u & unit_set) for h, u in reached.items()
            if h.suffix != ".cpp"}
    return headers, cpps


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
    counts, cpp_reach = fanout()
    if "--top" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--top") + 1])
        for name, number in sorted(counts.items(), key=lambda item: -item[1])[:limit]:
            print(f"{number:5d} headers {cpp_reach[name]:5d} cpps {name}")
        return 0
    if "--update" in sys.argv:
        lines = [f"# header headers-reaching-it (>= {LIMIT} headers); may only go down "
                 "(see check_header_fanout.py)"]
        lines += [f"{name} {number}" for name, number in sorted(counts.items())]
        BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 0
    baseline = read_baseline()
    failures = [f"{name}: is reached by {number} headers, baseline "
                f"{baseline.get(name, 'none (limit %d)' % LIMIT)}; split it, forward-declare, "
                f"or include it from fewer headers" for name, number in sorted(counts.items())
                if number > baseline.get(name, LIMIT - 1)]
    for name, number in sorted(baseline.items()):
        if counts.get(name, 0) < number:
            print(f"note: {name} dropped to {counts.get(name, 0)}; run --update to lower "
                  f"the baseline ({number})")
    if failures:
        print("\n".join(failures))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
