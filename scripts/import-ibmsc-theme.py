#!/usr/bin/env python3
"""Reproduce field schemes from the pinned iBMSC XML fixtures, without network access."""
import argparse
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "06aaf7438dbca3c8a0611bd13d134f6d1ce04a28"
REPOSITORY = "https://github.com/Shimi9999/pBMSC"
NAMES = {"BMSE": "BMSE", "BMSEmdk": "BMSE mdk", "BMSEmdkp": "BMSE mdkp",
         "Forme": "Forme", "IIDX": "IIDX", "MyO2_4K": "MyO2 4K", "O2Mania": "O2Mania", "Pomu": "Pomu"}
TIMING = {"BPM": "bpm", "STOP": "stop", "SCROLL": "scroll"}


def argb(value, opacity=1.0):
    number = int(value) & 0xffffffff
    alpha = round(((number >> 24) & 255) * opacity)
    return f"#{(number >> 16) & 255:02x}{(number >> 8) & 255:02x}{number & 255:02x}{alpha:02x}"


def column_colors(column, opacity, title_color):
    attributes = column.attrib
    return {"lane": argb(attributes["BG"]), "lane_alternate": argb(attributes["BG"]),
            "note": argb(attributes["NoteColor"], opacity),
            "hold_body": argb(attributes["LongNoteColor"], opacity),
            "long_note": argb(attributes["LongNoteColor"], opacity),
            "label": argb(attributes["TextColor"]), "long_label": argb(attributes["LongTextColor"]),
            "lane_label": title_color}


def convert(path, name):
    # Vendored fixtures are UTF-16, including their original encoding declaration.
    source = ET.fromstring(path.read_text(encoding="utf-16"))
    visual = {element.tag: element.attrib["Value"] for element in source.find("VisualSettings")
              if "Value" in element.attrib}
    opacity = float(visual["kOpacity"])
    if not 0 <= opacity <= 1:
        raise ValueError(f"{path}: invalid kOpacity")
    mapping = {"Bg": "viewport", "Grid": "beat_line", "Sub": "subgrid_line", "MLine": "measure_line",
               "VLine": "lane_line", "ColumnTitle": "lane_label", "kSelected": "selection_outline",
               "kMouseOver": "hover_border", "kMouseOverE": "erase_border", "SelBox": "selection_box",
               "TSMouseOver": "timing_hover", "TSSel": "timing_selected"}
    roles = {role: argb(visual[key]) for key, role in mapping.items() if key in visual}
    # A selected or hovered note retains its scheme fill; these colors are outlines in iBMSC.
    roles["selection_box_border"] = roles["selection_box"]
    roles["selection_add"] = roles["selection_toggle"] = roles["selection_box"]
    roles["selection_add_border"] = roles["selection_toggle_border"] = roles["selection_box_border"]
    roles["subgrid_emphasis"] = roles["subgrid_line"]
    roles["grid"] = roles["bgm_line"] = roles["lane_line"]
    roles["band"] = roles["band_split"] = roles["viewport"]
    roles["band_top"] = roles["measure_line"]
    roles["band_bottom"] = roles["beat_line"]
    roles["hold_hover"] = roles["hover_border"]
    roles["arrow_selection"] = roles["selection_outline"]
    result = {"version": 2, "name": f"iBMSC · {NAMES[name]}",
              "source": {"repository": REPOSITORY, "commit": COMMIT, "file": f"misc/Data/{name}.Theme.xml"},
              "roles": roles, "keys": {"1p": {}, "2p": {}}, "timing": {}}
    unknown = []
    for column in source.find("Columns"):
        title = column.attrib["Title"]
        colors = column_colors(column, opacity, roles["lane_label"])
        match = re.fullmatch(r"([AD])([1-8])", title)
        if match:
            player = "1p" if match[1] == "A" else "2p"
            position = "scratch" if match[2] == "8" else match[2]
            result["keys"][player][position] = colors
        elif title in TIMING:
            result["timing"][TIMING[title]] = colors
        elif title == "BGM" or re.fullmatch(r"B\d*", title):
            result["bgm"] = colors
        elif title not in ("", "Measure", "BGA", "LAYER", "POOR"):
            unknown.append(title)
    # Background, marker and hover roles with no direct iBMSC counterpart use Wormedit defaults.
    result["source"]["unknown_columns"] = sorted(set(unknown))
    result["source"]["fallback"] = "Missing roles, key positions and timing kinds inherit Wormedit. Fonts and widths are not imported."
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="compare regeneration byte for byte")
    parser.add_argument("--source-dir", type=Path, default=ROOT / "scripts/fixtures/ibmsc")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/themes/field")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    errors = []
    for name in NAMES:
        document = convert(args.source_dir / f"{name}.Theme.xml", name)
        contents = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
        target = args.output_dir / f"ibmsc-{name.lower().replace('_', '-')}.json"
        if args.check:
            if not target.exists() or target.read_text() != contents:
                errors.append(str(target))
        else:
            target.write_text(contents)
    if errors:
        parser.exit(1, "Generated schemes differ: " + ", ".join(errors) + "\n")
    print(f"{'Verified' if args.check else 'Generated'} {len(NAMES)} pinned iBMSC field schemes")


if __name__ == "__main__":
    main()
