#!/usr/bin/env python3
"""Ratchet: a (project, chart, revision) triple must travel as worm_ui_compose_target_t.

UI actions that need a document target go through worm_ui_frontend_run_targeted_action, whose table in
frontend.cpp owns the target check and the input gate. This test fails when a new std::function service
or frontend entry point takes the three loose integers again (a swapped argument compiles silently).
Fix by adding a worm_ui_targeted_action_t value instead. ALLOWED lists the remaining legacy sites; it
may only shrink.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Services and entry points that still take three loose integers, with the count allowed per file.
ALLOWED = {
    # compose_bgm_lane_width is a query, not an action; the stage_sound_* entry points are staged
    # operations with their own revision checks and still take three integers.
    "src/sys/app/application_command_state.h": 1,
    "src/ui/frontend/runtime.h": 5,
    "src/ui/nuklear/app/frontend.cpp": 4,
}
SERVICE = re.compile(r"std::function<[^;>]*\(\s*(?:std::)?uint64_t\s*,\s*(?:std::)?uint64_t\s*,\s*(?:std::)?uint64_t")
ENTRY2 = re.compile(r"\bworm_ui_frontend_\w+\s*\([^)]*\buint64_t\s+project\w*\s*,\s*uint64_t\s+chart\w*\s*,\s*uint64_t\s+\w+")


def main() -> int:
    failures = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        name = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8")
        found = len(SERVICE.findall(text)) + len(ENTRY2.findall(text))
        allowed = ALLOWED.get(name, 0)
        if found > allowed:
            failures.append(f"{name}: {found} loose (project, chart, id/revision) integer triple(s), "
                            f"{allowed} allowed; use worm_ui_compose_target_t and worm_ui_targeted_action_t")
        elif found < allowed:
            failures.append(f"{name}: only {found} triple(s) left, lower ALLOWED to {found}")
    for line in failures:
        print(line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
