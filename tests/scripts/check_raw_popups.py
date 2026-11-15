#!/usr/bin/env python3
"""Ratchet: popups go through the chrome popup components.

Every popup shares one surface (rounded panel, soft shadow, menu padding, no stock square border).
Use wormedit::ui::chrome::surface_popup_begin/end for a plain popup, text_prompt_begin/body/end for
a one-line prompt, begin_form_popup/end_form_popup for a retained form popup, or the context menu
and dialog components. A bare nk_popup_begin under src/ui/nuklear is only allowed in the files that
implement those components, listed below; there is no baseline to lower, so any other use fails.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ALLOWED = {
    "src/ui/nuklear/chrome/chrome.h",
    "src/ui/nuklear/chrome/popup_lifecycle.h",
    "src/ui/nuklear/chrome/context_menu.cpp",
}
PATTERN = re.compile(r"\bnk_popup_begin\s*\(")


def main() -> int:
    failures = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        name = path.relative_to(ROOT).as_posix()
        if name in ALLOWED:
            continue
        found = len(PATTERN.findall(path.read_text(encoding="utf-8")))
        if found:
            failures.append(f"{name}: {found} bare nk_popup_begin call(s); use chrome::surface_popup_begin "
                            f"(or text_prompt_begin / begin_form_popup)")
    for line in failures:
        print(line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
