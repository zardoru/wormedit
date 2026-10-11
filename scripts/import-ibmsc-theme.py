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
# These BMS skins store the left scratch first and the right scratch last.
LEFT_SCRATCH_FIRST = {"BMSE", "BMSEmdk", "Forme", "IIDX"}
POPN_SKINS = {"BMSEmdkp", "Pomu"}
POPN_CLASSES = {"LW": ("white", "pms_white"), "LY": ("pms_yellow",),
                "LG": ("pms_green",), "LB": ("black", "pms_blue"),
                "RED": ("scratch", "pms_red")}


def key_position(name, title):
    match = re.fullmatch(r"([AD])([1-8])", title)
    if not match or name in POPN_SKINS:
        return None
    player = "1p" if match[1] == "A" else "2p"
    number = int(match[2])
    scratch = 1 if name in LEFT_SCRATCH_FIRST and player == "1p" else 8
    position = "scratch" if number == scratch else str(number - (scratch == 1))
    return player, position


def argb(value):
    number = int(value) & 0xffffffff
    alpha = (number >> 24) & 255
    return f"#{(number >> 16) & 255:02x}{(number >> 8) & 255:02x}{number & 255:02x}{alpha:02x}"


def column_colors(column, title_color):
    attributes = column.attrib
    return {"lane": argb(attributes["BG"]), "lane_alternate": argb(attributes["BG"]),
            "note": argb(attributes["NoteColor"]),
            "hold_body": argb(attributes["LongNoteColor"]),
            "long_note": argb(attributes["LongNoteColor"]),
            "label": argb(attributes["TextColor"]), "long_label": argb(attributes["LongTextColor"]),
            "lane_label": title_color}


def convert(path, name):
    # Vendored fixtures are UTF-16, including their original encoding declaration.
    source = ET.fromstring(path.read_text(encoding="utf-16"))
    visual = {element.tag: element.attrib["Value"] for element in source.find("VisualSettings")
              if "Value" in element.attrib}
    hidden_opacity = float(visual["kOpacity"])
    if not 0 <= hidden_opacity <= 1:
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
        # PanelDraw.vb applies kOpacity only to hidden notes, never normal fills.
        # Wormedit handles invisible-note dimming separately in compose_kind_fill.
        colors = column_colors(column, roles["lane_label"])
        position = key_position(name, title)
        if position:
            player, key = position
            result["keys"][player][key] = colors
        elif name in POPN_SKINS and title in POPN_CLASSES:
            for role in POPN_CLASSES[title]:
                result.setdefault("key_colors", {})[role] = colors
        elif name in POPN_SKINS and (re.fullmatch(r"[AD][1-8]", title) or
                                    title in {"RW", "RY", "RG", "RB"}):
            # Hidden A/D placeholders are not playable colors in these pop'n skins.
            pass
        elif title in TIMING:
            result["timing"][TIMING[title]] = colors
        elif title == "BGM" or re.fullmatch(r"B\d*", title):
            result["bgm"] = colors
        elif title not in ("", "Measure", "BGA", "LAYER", "POOR"):
            unknown.append(title)
    # Background, marker and hover roles with no direct iBMSC counterpart use Wormedit defaults.
    result["source"]["unknown_columns"] = sorted(set(unknown))
    result["source"]["fallback"] = "Missing roles, key positions and timing kinds inherit Wormedit. Fonts and widths are not imported."
    # Wormedit presentation default: BMSE DP5 previews both scratches on the left.
    if name == "BMSE":
        result["scratch_placement"] = {"1p": "left", "2p": "left"}
    if name in LEFT_SCRATCH_FIRST:
        result["source"]["fallback"] += " Source scratch columns A1/D8 are bound by semantic lane role."
    if name in POPN_SKINS:
        result["source"]["fallback"] += " Named pop'n colors bind by key class; white/blue/red also supply BMS white/black/scratch roles."
    if name == "MyO2_4K":
        keys = result["keys"]["1p"]
        result["numbered_layout"] = {
            "stages": [["white", "blue", "blue", "white"]],
            "colors": [[keys[str(key)] for key in range(1, 5)]]}
    elif name in POPN_SKINS:
        imported = result.pop("key_colors")
        result["lane_classes"] = {name: imported[role] for name, role in
            (("white", "pms_white"), ("blue", "pms_blue"), ("green", "pms_green"),
             ("yellow", "pms_yellow"), ("red", "pms_red"))}
    else:
        keys = result["keys"]["1p"]
        result["lane_classes"] = {"white": keys["1"], "blue": keys["2"], "red": keys["scratch"]}
        if name == "O2Mania":
            result["lane_classes"].update(green=keys["4"], yellow=keys["4"])
    result.pop("keys")
    result.pop("scratch_placement", None)
    if name == "BMSE": result["rotate_dp5"] = True
    result["source"]["fallback"] = ("Semantic lane classes inherit missing colors from Wormedit; "
        "numbered colors apply only to their exact supported stage/class layout. "
        "Fonts and widths are not imported.")
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
