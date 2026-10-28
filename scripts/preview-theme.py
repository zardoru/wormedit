#!/usr/bin/env python3
"""Validate the editable palette and render a standalone swatch/gradient sheet."""

import argparse
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADES = (50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950)


def luminance(value):
    channels = [int(value[index:index + 2], 16) / 255 for index in (1, 3, 5)]
    linear = [channel / 12.92 if channel <= 0.04045 else
              ((channel + 0.055) / 1.055) ** 2.4 for channel in channels]
    return sum(weight * channel for weight, channel in zip((0.2126, 0.7152, 0.0722), linear))


def contrast(first, second):
    a, b = sorted((luminance(first), luminance(second)), reverse=True)
    return (a + 0.05) / (b + 0.05)


def validate(palette):
    scales = palette["scales"]
    for name, scale in scales.items():
        if tuple(int(shade) for shade in scale) != SHADES:
            raise ValueError(f"{name}: expected shades {SHADES}")
        for shade, value in scale.items():
            if len(value) != 7 or value[0] != "#" or any(
                    char not in "0123456789abcdefABCDEF" for char in value[1:]):
                raise ValueError(f"{name}.{shade}: invalid color {value}")
    if scales["neutral"]["950"].lower() != "#12131a":
        raise ValueError("neutral.950 must remain #12131a")
    if scales["blue"]["600"].lower() != "#145ce3":
        raise ValueError("blue.600 must remain #145ce3")
    for name, token in palette["roles"].items():
        if token.startswith("$"):
            scale, shade = token[1:].split(".", 1)
            if scale not in scales or shade not in scales[scale]:
                raise ValueError(f"{name}: unknown token {token}")


def render(palette):
    sections = []
    for name, scale in palette["scales"].items():
        gradient = ", ".join(scale[str(shade)] for shade in reversed(SHADES))
        swatches = "".join(
            f'<div class="swatch"><div class="chip" style="background:{value}"></div>'
            f'<b>{shade}</b><code>{value}</code></div>'
            for shade, value in scale.items())
        sections.append(f'<section><h2>{html.escape(name)}</h2>'
                        f'<div class="gradient" style="background:linear-gradient(90deg,{gradient})"></div>'
                        f'<div class="swatches">{swatches}</div></section>')
    roles = "".join(f'<tr><td>{html.escape(name)}</td><td>{html.escape(value)}</td></tr>'
                    for name, value in palette["roles"].items())
    contrast_text = (f'White on blue.600: {contrast("#ffffff", "#145ce3"):.2f}:1 · '
                     f'neutral.200 on neutral.950: '
                     f'{contrast(palette["scales"]["neutral"]["200"], "#12131a"):.2f}:1')
    return f'''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Wormedit color scales</title><style>
*{{box-sizing:border-box}}body{{background:#12131a;color:#ced2df;font:15px system-ui,sans-serif;
margin:3rem auto;max-width:1100px;padding:0 1.5rem}}h1,h2{{color:#f5f6fa}}
section{{margin:2.5rem 0}}.gradient{{height:40px;border-radius:8px;margin:1rem 0}}
.swatches{{display:grid;grid-template-columns:repeat(11,minmax(0,1fr));gap:8px}}
.chip{{height:72px;border-radius:5px;border:1px solid #555b6f}}
.swatch code,.swatch b{{display:block;font-size:12px;margin-top:5px}}
table{{border-collapse:collapse}}td{{padding:6px 24px 6px 0;border-bottom:1px solid #3a3c4b}}
</style><h1>Wormedit color scales</h1><p>{contrast_text}</p>
{''.join(sections)}<h2>UI roles</h2><table>{roles}</table></html>'''


def resolve_color(document, token):
    if not isinstance(token, str):
        raise ValueError("expected a color string")
    if token.startswith("$"):
        scale, shade = token[1:].split(".", 1)
        token = document["scales"][scale][shade]
    if len(token) not in (7, 9) or token[0] != "#":
        raise ValueError(f"invalid color {token}")
    int(token[1:], 16)
    return token


def interface_roles(document, required):
    if not isinstance(document.get("name"), str) or not document["name"].strip():
        raise ValueError("expected an interface theme name")
    roles = document.get("roles")
    if not isinstance(roles, dict) or set(roles) != set(required):
        raise ValueError("expected every interface color role and no unknown roles")
    return {name: resolve_color(document, token) for name, token in roles.items()}


def interface_contrast(name, roles):
    pairs = [(text, background) for text in ("text", "text_bright", "text_muted")
             for background in ("background", "surface", "surface_hover", "surface_active", "popup")]
    pairs += [("selected_text", "selected"), ("status", "background"),
              ("accent", "surface"), ("conflict", "background"),
              ("waveform_peak", "waveform_background"),
              ("waveform_playhead", "waveform_background")]
    warnings = []
    for foreground, background in pairs:
        base = opaque(roles[background], "#ffffff")
        ratio = contrast(opaque(roles[foreground], base), base)
        if ratio < 3:
            warnings.append(f"{name}: {foreground} on {background}: {ratio:.2f}:1")
    return warnings


def render_interface(document, roles):
    variables = ";".join(f"--{name.replace('_', '-')}:{value}" for name, value in roles.items())
    swatches = "".join(f'<span title="{html.escape(name)}: {value}" style="background:{value}"></span>'
                       for name, value in roles.items())
    return f'''<article style="{variables}"><h2>{html.escape(document['name'])}</h2>
<nav>File　Edit　View <span>Compose</span></nav>
<div class="toolbar"><button>Open…</button><button class="primary">Play</button>
<button class="hover">Hover</button><button class="active">Pressed</button></div>
<div class="content"><aside><div class="selected">Selected chart</div><div>Sound library</div>
<small>12 charts · 128 BPM</small></aside><div class="editor">Title <span class="selection">Selected text</span>
<div class="wave"><svg viewBox="0 0 200 42" role="img" aria-label="Waveform colors">
<path d="M0 21H200" stroke="var(--waveform-guide)"/>
<path d="M5 14V28M15 7V35M25 12V30M35 17V25M45 9V33M55 3V39M65 11V31M75 16V26M85 6V36M95 13V29M105 17V25M115 11V31M125 2V40M135 8V34M145 14V28M155 10V32M165 4V38M175 12V30M185 16V26M195 10V32" stroke="var(--waveform-peak)" stroke-width="3"/>
<path d="M5 18V24M15 15V27M25 17V25M45 16V26M55 12V30M65 17V25M85 13V29M95 17V25M115 16V26M125 11V31M135 14V28M145 17V25M155 16V26M165 13V29M175 17V25M195 16V26" stroke="var(--waveform-rms)" stroke-width="3"/>
<path d="M117 0V42" stroke="var(--waveform-playhead)" stroke-width="2"/></svg></div></div></div>
<footer><span class="accent">● Ready</span><span class="conflict">Conflict</span><span>100%</span></footer>
<div class="swatches">{swatches}</div></article>'''


def preview_interfaces(output, default):
    cards, warnings = [], []
    paths = sorted((ROOT / "assets/themes").glob("ui-*.json"))
    for path in paths:
        document = json.loads(path.read_text())
        roles = interface_roles(document, default["roles"])
        cards.append(render_interface(document, roles))
        warnings.extend(interface_contrast(path.stem, roles))
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Wormedit interface palettes</title><style>
*{box-sizing:border-box}body{margin:0;color:var(--foreground,#ced2df);font:13px system-ui,sans-serif}
.intro{color:var(--muted-foreground,#8b92a9);margin:0 0 16px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr));gap:16px}
article{background:var(--background);color:var(--text);padding:16px;border:1px solid var(--border);border-radius:8px;min-width:0}
h2{color:var(--text-bright);font-size:16px;margin:0 0 14px}nav{border-bottom:1px solid var(--separator);padding-bottom:10px}
nav span{float:right;color:var(--text-muted)}.toolbar{display:flex;gap:7px;padding:12px 0;flex-wrap:wrap}
button{font:inherit;background:var(--surface);color:var(--text);border:1px solid var(--border);border-radius:4px;padding:6px 10px}
.primary,.selected,.selection{background:var(--selected);color:var(--selected-text)}.primary{border-color:var(--border-hover)}
.hover{background:var(--surface-hover);color:var(--text-bright)}.active{background:var(--surface-active);color:var(--text-bright)}
.content{display:grid;grid-template-columns:1fr 1fr;gap:10px}.content>aside,.editor{background:var(--surface);border:1px solid var(--border);border-radius:4px;padding:8px;min-width:0}
aside>div{padding:6px}.selected{border-radius:3px}small{display:block;color:var(--text-muted);padding:8px 6px;font-size:11px}
.selection{padding:2px;display:block;margin:6px 0}.wave{background:var(--waveform-background);padding:6px;margin-top:10px}svg{display:block;width:100%;height:42px}
footer{display:flex;justify-content:space-between;color:var(--status);font-size:11px;margin-top:12px}.accent{color:var(--accent)}.conflict{color:var(--conflict)}
.swatches{display:flex;margin-top:12px;height:8px;overflow:hidden;border-radius:3px}.swatches span{flex:1}
</style><body><p class="intro">Palette study using Wormedit’s theme roles. Existing Nuklear geometry is retained.</p><div class="grid">'''
    page += "".join(cards) + "</div></body></html>"
    target = output.parent / (output.stem + "-interfaces.html")
    target.write_text(page)
    for warning in warnings:
        print("CONTRAST " + warning)
    print(f"Validated {len(paths)} interface themes; {len(warnings)} contrast warnings; comparison: {target}")
    return warnings


def validate_field(document):
    if document.get("version") != 2 or not isinstance(document.get("name"), str) or not document["name"]:
        raise ValueError("expected schema version 2 and a name")
    def walk(value):
        if isinstance(value, dict):
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)
        elif isinstance(value, str):
            resolve_color(document, value)
        else:
            raise ValueError("expected a field color")
    for section in ("roles", "key_colors", "lane_classes", "bgm", "timing", "note_accents"):
        if section in document:
            walk(document[section])
    classes = {"white", "blue", "green", "yellow", "red"}
    if "keys" in document or "key_count" in document:
        raise ValueError("ungated positional keys are unsupported")
    if set(document.get("lane_classes", {})) - classes:
        raise ValueError("unknown semantic lane class")
    if "rotate_dp5" in document and not isinstance(document["rotate_dp5"], bool):
        raise ValueError("rotate_dp5 must be boolean")
    if "numbered_layout" in document:
        numbered = document["numbered_layout"]
        stages, colors = numbered["stages"], numbered["colors"]
        if not isinstance(stages, list) or not 1 <= len(stages) <= 2 or len(colors) != len(stages):
            raise ValueError("numbered_layout requires matching stages and colors")
        for names, entries in zip(stages, colors):
            if not names or len(names) != len(entries) or any(name not in classes for name in names):
                raise ValueError("numbered_layout requires matching semantic classes")
        walk(colors)
    if "note_accents" in document and len(document["note_accents"]) != 8:
        raise ValueError("expected eight note accents")


def opaque(color, background):
    color = color + "ff" if len(color) == 7 else color
    alpha = int(color[7:9], 16) / 255
    return "#" + "".join(f"{round(int(color[i:i+2],16)*alpha + int(background[i:i+2],16)*(1-alpha)):02x}"
                         for i in (1, 3, 5))


def field_entries(document, default):
    def resolved(doc, entries):
        return {key: resolve_color(doc, value) for key, value in entries.items()}
    roles = resolved(default, default["roles"])
    roles.update(resolved(document, document.get("roles", {})))
    generic = resolved(default, default["key_colors"]["generic"])
    entries = {}
    for kind, values in default["key_colors"].items():
        entries["key-color/" + kind] = {**generic, **resolved(default, values),
            **resolved(document, document.get("key_colors", {}).get(kind, {}))}
    for name, values in default["lane_classes"].items():
        entries["class/" + name] = {**generic, **resolved(default, values),
            **resolved(document, document.get("lane_classes", {}).get(name, {}))}
    numbered = document.get("numbered_layout", {})
    for stage, (names, colors) in enumerate(zip(numbered.get("stages", []), numbered.get("colors", []))):
        for position, (name, values) in enumerate(zip(names, colors)):
            entries[f"numbered-stage-{stage + 1}/{position + 1} ({name})"] = {
                **entries["class/" + name], **resolved(document, values)}
    entries["BGM"] = {**generic, **resolved(default, default["bgm"]), **resolved(document, document.get("bgm", {}))}
    for kind, values in document.get("timing", {}).items():
        entries["timing/" + kind] = {**generic, **resolved(document, values)}
    return roles, entries


def render_field(document, default, path):
    roles, entries = field_entries(document, default)
    rows, warnings = [], []
    for name, entry in entries.items():
        background = opaque(entry["lane"], roles["viewport"])
        note = opaque(entry["note"], background)
        hold = opaque(entry["hold_body"], background)
        label = entry.get("label", "#101824" if sum(int(entry["note"][i:i+2],16)*w for i,w in ((1,299),(3,587),(5,114))) // 1000 >= 150 else "#f8fbff")
        long_label = entry.get("long_label", label)
        checks = [("lane_label", entry.get("lane_label", roles["lane_label"]), background),
                  ("label", label, note), ("long_label", long_label, hold)]
        for role, foreground, bg in checks:
            ratio = contrast(opaque(foreground, bg), bg)
            if ratio < 3:
                warnings.append(f"{path.name} {name}/{role}: {ratio:.2f}:1")
        chips = "".join(f'<td style="background:{html.escape(value)}">{html.escape(key)}<br>{html.escape(value)}</td>'
                        for key, value in entry.items())
        rows.append(f'<tr><th>{html.escape(name)}</th>{chips}</tr>')
    role_rows = "".join(f'<tr><th>{html.escape(name)}</th><td style="background:{value}">{value}</td></tr>'
                        for name, value in roles.items())
    title = html.escape(document["name"])
    body = f"""<!doctype html><html lang="en"><meta charset="utf-8"><title>{title}</title>
<style>body{{font:14px system-ui;background:#12131a;color:white;margin:2rem}}td,th{{padding:14px;border:1px solid #666}}
td{{min-width:100px;text-shadow:0 1px 2px black}}table{{border-collapse:collapse}}th{{text-align:left}}</style>
<h1>{title}</h1><p>Schema 2 · composited label contrast below 3:1 is reported without changing colors.</p>
<table>{''.join(rows)}</table><h2>Field roles</h2><table>{role_rows}</table>
<h2>Contrast warnings</h2><pre>{html.escape(chr(10).join(warnings))}</pre></html>"""
    return body, warnings


def preview_fields(output):
    directory = ROOT / "assets/themes/field"
    default = json.loads((directory / "wormedit.json").read_text())
    target = output.parent / (output.stem + "-fields")
    target.mkdir(parents=True, exist_ok=True)
    warnings = []
    paths = sorted(directory.glob("*.json"))
    for path in paths:
        document = json.loads(path.read_text())
        validate_field(document)
        body, found = render_field(document, default, path)
        (target / (path.stem + ".html")).write_text(body)
        warnings.extend(found)
    for warning in warnings:
        print("CONTRAST " + warning)
    print(f"Validated {len(paths)} field schemes; {len(warnings)} contrast warnings; swatch sheets: {target}")
    return warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/wormedit-theme-preview.html"))
    args = parser.parse_args()
    palette = json.loads((ROOT / "assets/themes/ui-dark.json").read_text())
    validate(palette)
    args.output.write_text(render(palette))
    print(args.output)
    preview_interfaces(args.output, palette)
    preview_fields(args.output)


if __name__ == "__main__":
    main()
