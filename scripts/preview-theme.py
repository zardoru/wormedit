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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/wormedit-theme-preview.html"))
    args = parser.parse_args()
    palette = json.loads((ROOT / "assets/themes/ui-dark.json").read_text())
    validate(palette)
    args.output.write_text(render(palette))
    print(args.output)


if __name__ == "__main__":
    main()
