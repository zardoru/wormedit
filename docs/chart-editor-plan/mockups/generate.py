#!/usr/bin/env python3
"""Generate the reference UI mockups in this directory as SVG.

Colors come from assets/themes/ui-dark.json roles so the mockups track the real palette.
Run: python3 docs/chart-editor-plan/mockups/generate.py  (writes *.svg next to this file)
"""

import html
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ui-dark.json roles (resolved).
BG = "#12131a"          # background
SURFACE = "#1e1f28"     # surface
HOVER = "#2c2e3c"       # surface_hover
ACTIVE = "#3a3c4b"      # surface_active
BORDER = "#3a3c4b"      # border
BORDER_HI = "#86b4ff"   # border_hover
TEXT = "#ced2df"        # text
BRIGHT = "#f5f6fa"      # text_bright
MUTED = "#adb3c5"       # text_muted
FAINT = "#717077"       # neutral.500
SELECTED = "#145ce3"    # selected (primary)
ACCENT = "#c9d35e"      # accent (lime.300)
CORAL = "#df563b"       # coral.500 (destructive / conflict)
AMBER = "#f2be66"       # note accent 0 (displace)
FIELD = "#161921"       # compose viewport
GRID = "#3a4353"
FONT = "Adwaita Mono, DejaVu Sans Mono, monospace"


class Svg:
    def __init__(self, width, height, background=BG):
        self.width, self.height = width, height
        self.parts = [f'<rect width="{width}" height="{height}" fill="{background}"/>']

    def rect(self, x, y, w, h, fill="none", stroke=None, radius=0, width=1, opacity=None, dash=None):
        attrs = f'x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{fill}"'
        if stroke:
            attrs += f' stroke="{stroke}" stroke-width="{width}"'
        if opacity is not None:
            attrs += f' opacity="{opacity}"'
        if dash:
            attrs += f' stroke-dasharray="{dash}"'
        self.parts.append(f"<rect {attrs}/>")

    def line(self, x1, y1, x2, y2, stroke=BORDER, width=1, dash=None):
        extra = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="{width}"{extra}/>')

    def text(self, x, y, value, color=TEXT, size=13, anchor="start", weight="normal", outline=None):
        escaped = html.escape(value)
        if outline:
            self.parts.append(
                f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" '
                f'text-anchor="{anchor}" fill="none" stroke="{outline}" stroke-width="2.5" '
                f'stroke-linejoin="round">{escaped}</text>')
        self.parts.append(
            f'<text x="{x}" y="{y}" font-family="{FONT}" font-size="{size}" font-weight="{weight}" '
            f'text-anchor="{anchor}" fill="{color}">{escaped}</text>')

    def circle(self, cx, cy, r, fill, stroke=None):
        extra = f' stroke="{stroke}" stroke-width="1"' if stroke else ""
        self.parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"{extra}/>')

    def path(self, d, fill="none", stroke=None, width=1):
        extra = f' stroke="{stroke}" stroke-width="{width}"' if stroke else ""
        self.parts.append(f'<path d="{d}" fill="{fill}"{extra}/>')

    def note(self, x, y, value, color=MUTED, size=11):
        """Annotation in the margin (not part of the UI)."""
        self.text(x, y, value, color, size)

    def save(self, name):
        body = "\n  ".join(self.parts)
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" height="{self.height}" '
               f'viewBox="0 0 {self.width} {self.height}">\n  {body}\n</svg>\n')
        (HERE / name).write_text(svg, encoding="utf-8")


# Shared widgets ----------------------------------------------------------------------------------

def panel(svg, x, y, w, h):
    """Menu/dialog surface: soft shadow, 8 px radius, 1 px border_hover stroke."""
    for step in range(1, 9):
        svg.rect(x - step, y - step + 4, w + 2 * step, h + 2 * step, "#000000", radius=8 + step,
                 opacity=0.035)
    svg.rect(x, y, w, h, SURFACE, BORDER_HI, radius=8)


def button(svg, x, y, w, label, kind="secondary", h=28, disabled=False):
    fill, stroke, color = HOVER, BORDER, TEXT
    if kind == "primary":
        fill, stroke, color = SELECTED, SELECTED, "#ffffff"
    elif kind == "destructive":
        fill, stroke, color = "none", CORAL, "#ff9e86"
    elif kind == "ghost":
        fill, stroke, color = "none", None, TEXT
    if disabled:
        fill, stroke, color = SURFACE, BORDER, FAINT
    svg.rect(x, y, w, h, fill, stroke, radius=4)
    svg.text(x + w / 2, y + h / 2 + 4.5, label, color, anchor="middle")


def icon_button(svg, x, y, glyph, active=False, size=28, tip=None):
    svg.rect(x, y, size, size, ACCENT if active else HOVER, None if active else BORDER, radius=4)
    svg.text(x + size / 2, y + size / 2 + 5, glyph, "#12131a" if active else TEXT, 14, "middle")


def segmented(svg, x, y, labels, active, seg_w=None, h=26):
    widths = [seg_w or (len(label) * 8 + 20) for label in labels]
    total = sum(widths)
    svg.rect(x, y, total, h, HOVER, BORDER, radius=4)
    cursor = x
    for index, (label, width) in enumerate(zip(labels, widths)):
        if index == active:
            svg.rect(cursor + 2, y + 2, width - 4, h - 4, SELECTED, radius=3)
        elif index:
            svg.line(cursor, y + 5, cursor, y + h - 5, BORDER)
        svg.text(cursor + width / 2, y + h / 2 + 4.5, label,
                 "#ffffff" if index == active else TEXT, 12, "middle")
        cursor += width
    return total


def chip(svg, x, y, label, active=False, h=24, dashed=False):
    width = len(label) * 8 + 16
    svg.rect(x, y, width, h, SELECTED if active else HOVER, BORDER_HI if active else BORDER,
             radius=h / 2, dash="3 2" if dashed else None)
    svg.text(x + width / 2, y + h / 2 + 4, label, "#ffffff" if active else TEXT, 12, "middle")
    return width


def checkbox(svg, x, y, state):
    """state: 'on', 'off', 'mixed'."""
    if state == "off":
        svg.rect(x, y, 16, 16, "none", BORDER_HI, radius=3, width=1.5)
        return
    svg.rect(x, y, 16, 16, SELECTED, SELECTED, radius=3, width=1.5)
    if state == "on":
        svg.path(f"M{x + 3.5} {y + 8.5} L{x + 6.8} {y + 11.8} L{x + 12.5} {y + 4.8}", stroke="#ffffff", width=2)
    else:
        svg.line(x + 4, y + 8, x + 12, y + 8, "#ffffff", 2)


def callout(svg, x, y, number):
    svg.circle(x, y, 9, CORAL)
    svg.text(x, y + 4, str(number), "#ffffff", 11, "middle", "bold")


# Mockups -----------------------------------------------------------------------------------------

def unsaved_changes():
    svg = Svg(620, 300, "#0b0c11")
    # Dimmed workspace behind the modal.
    for index in range(9):
        svg.rect(40 + index * 36, 20, 34, 260, "#1a1c24" if index % 2 else "#16171e")
    svg.rect(0, 0, 620, 300, "#000000", opacity=0.35)
    x, y, w, h = 110, 85, 400, 132
    panel(svg, x, y, w, h)
    svg.text(x + 16, y + 30, "Unsaved changes", BRIGHT, 14, weight="bold")
    svg.text(x + 16, y + 58, "Save your changes before quitting WormEdit?", TEXT)
    button(svg, x + 16, y + h - 44, 150, "Discard changes", "destructive")
    button(svg, x + w - 16 - 84 - 8 - 84, y + h - 44, 84, "Cancel")
    button(svg, x + w - 16 - 84, y + h - 44, 84, "Save", "primary")
    svg.note(x, y + h + 26, "16 px padding all round · no title bar · Esc = Cancel · Enter = Save")
    svg.note(x, y + h + 42, "height fits content · backdrop rgba(0,0,0,.35) · no stray status text")
    svg.save("unsaved-changes.svg")


def tap_timing():
    svg = Svg(700, 430, "#0b0c11")
    x, y, w, h = 70, 30, 420, 330
    panel(svg, x, y, w, h)
    svg.text(x + 16, y + 30, "Tap tempo", BRIGHT, 14, weight="bold")
    svg.text(x + w - 22, y + 30, "✕", MUTED, 14, "middle")
    # Beat ring with the last-tapped segment flashing.
    cx, cy, r = x + 16 + 48, y + 100, 46
    import math
    for index in range(4):
        start = -math.pi / 2 + index * math.pi / 2 + 0.05
        end = start + math.pi / 2 - 0.1
        inner = 28
        p1 = (cx + r * math.cos(start), cy + r * math.sin(start))
        p2 = (cx + r * math.cos(end), cy + r * math.sin(end))
        p3 = (cx + inner * math.cos(end), cy + inner * math.sin(end))
        p4 = (cx + inner * math.cos(start), cy + inner * math.sin(start))
        color = ACCENT if index == 1 else HOVER
        svg.path(f"M{p1[0]:.1f} {p1[1]:.1f} A{r} {r} 0 0 1 {p2[0]:.1f} {p2[1]:.1f} "
                 f"L{p3[0]:.1f} {p3[1]:.1f} A{inner} {inner} 0 0 0 {p4[0]:.1f} {p4[1]:.1f} Z", color)
    svg.text(x + 140, y + 92, "174.02", BRIGHT, 30, weight="bold")
    svg.text(x + 268, y + 92, "BPM", MUTED, 14)
    svg.text(x + 140, y + 116, "±1.8 ms · stable", "#aeb93d", 12)
    svg.text(x + 140, y + 134, "beat 0 at 12.3 ms", MUTED, 12)
    # Tap progress.
    by = y + 168
    for index in range(16):
        svg.rect(x + 16 + index * 18.5, by, 15, 8, ACCENT if index < 9 else HOVER, radius=2)
    svg.text(x + w - 16, by + 8, "9 / 16 taps", MUTED, 12, "end")
    svg.text(x + 16, by + 34, "Tap T or Space on the beat", TEXT)
    svg.text(x + 16, by + 68, "Round to", MUTED, 12)
    segmented(svg, x + 96, by + 50, ["Raw", "1", "0.5"], 0, seg_w=56)
    svg.line(x + 16, y + h - 58, x + w - 16, y + h - 58, BORDER)
    button(svg, x + 16, y + h - 44, 72, "Reset", "ghost")
    button(svg, x + w - 16 - 140, y + h - 44, 140, "Apply 174 BPM", "primary")
    svg.note(x, y + h + 26, "hover Apply: “First tap sets the downbeat; later BPM changes keep their positions.”")
    svg.note(x, y + h + 42, "readout uses a 28–30 px display font · ring segment flashes 120 ms per tap")
    svg.save("tap-timing.svg")


def chart_table():
    svg = Svg(980, 360)
    x0 = 24
    svg.text(x0, 34, "Charts", BRIGHT, 16, weight="bold")
    svg.text(x0 + 74, 34, "3", MUTED, 13)
    svg.rect(980 - 24 - 32, 12, 32, 32, ACCENT, radius=6)
    svg.line(980 - 24 - 16, 20, 980 - 24 - 16, 36, "#12131a", 2.5)
    svg.line(980 - 24 - 24, 28, 980 - 24 - 8, 28, "#12131a", 2.5)
    columns = [("", 18), ("", 26), ("Name", 170), ("Author", 150), ("Rating", 66), ("Mode", 96),
               ("Keys", 46), ("Groups", 110), ("Notes", 66), ("Length", 60), ("", 90)]
    top = 56
    svg.rect(x0, top, 932, 30, SURFACE, BORDER, radius=4)
    cursor = x0
    for title, width in columns:
        svg.text(cursor + 4, top + 20, title, MUTED, 12)
        cursor += width
    checkbox(svg, x0 + 18 + 4, top + 7, "mixed")
    rows = [
        ("Simurgh [7K Another]", "BilliumMoto & So…", "12", "BME 7K", "7", "Unassigned", "1002", "2:30", "on", True),
        ("Simurgh [7K Hyper]", "BilliumMoto & So…", "9", "BME 7K", "7", "Unassigned", "744", "2:30", "off", False),
        ("Simurgh [14K]", "BilliumMoto & So…", "Unset", "BME 14K", "14", "DP set", "1388", "2:30", "on", False),
    ]
    for index, (name, author, rating, mode, keys, groups, notes, length, state, hovered) in enumerate(rows):
        y = top + 34 + index * 34
        fill = "#172e60" if state == "on" else (HOVER if hovered else None)
        if fill:
            svg.rect(x0, y, 932, 30, fill, radius=4)
        svg.text(x0 + 4, y + 20, "⠿", FAINT, 13)
        checkbox(svg, x0 + 18 + 4, y + 7, state)
        cursor = x0 + 44
        for value, (_, width) in zip([name, author, rating, mode, keys, groups, notes, length], columns[2:]):
            svg.text(cursor + 4, y + 20, value, TEXT if value != "Unset" else FAINT, 13)
            cursor += width
        if hovered:
            icon_button(svg, x0 + 932 - 70, y + 2, "↗", size=26)
            icon_button(svg, x0 + 932 - 38, y + 2, "⋯", size=26)
    # Context menu from the hovered row's ⋯.
    mx, my = x0 + 932 - 200, top + 34 + 30
    panel(svg, mx, my, 196, 100)
    for index, label in enumerate(["Open in Compose", "Duplicate"]):
        svg.text(mx + 12, my + 24 + index * 25, label, TEXT)
    svg.line(mx + 8, my + 62, mx + 188, my + 62, BORDER)
    svg.text(mx + 12, my + 84, "Delete…", "#ff9e86")
    # Selection bar.
    by = top + 34 + 3 * 34 + 14
    svg.rect(x0, by, 640, 38, SURFACE, BORDER_HI, radius=6)
    svg.text(x0 + 14, by + 24, "2 selected", BRIGHT)
    bx = x0 + 110
    for label in ["Set author…", "Set rating…", "Duplicate"]:
        button(svg, bx, by + 5, len(label) * 8 + 20, label)
        bx += len(label) * 8 + 28
    button(svg, bx, by + 5, 84, "Delete…", "destructive")
    svg.text(x0 + 640 - 18, by + 24, "✕", MUTED, 14, "middle")
    svg.note(x0, by + 66, "click row = select only · Ctrl/Shift extend · double-click opens · reorder only by dragging ⠿ or Alt+↑/↓")
    svg.note(x0, by + 82, "header box: all / none / mixed (replaces “Clear selection”) · long text ellipsized + tooltip")
    svg.save("chart-table.svg")


def quantize():
    svg = Svg(400, 420)
    x, y, w = 12, 12, 376
    svg.rect(x, y, w, 396, SURFACE, BORDER, radius=6)
    svg.text(x + 12, y + 26, "Quantize", BRIGHT, 13, weight="bold")
    svg.text(x + 100, y + 26, "12 sel → 8 move · 4 stay · 0 collide", MUTED, 11)
    # Grid on a grid: even subdivisions over their triplet counterparts, column-aligned.
    svg.text(x + 12, y + 54, "Grid", MUTED, 12)
    col_w, gx = 52, x + 84
    rows = [("even", ["1/4", "1/8", "1/16", "1/32", "1/64"]),
            ("triplet", ["1/6", "1/12", "1/24", "1/48", "1/96"])]
    for row_index, (name, labels) in enumerate(rows):
        ry = y + 40 + row_index * 30
        svg.text(gx - 8, ry + 16, name, FAINT, 10, "end")
        for col, label in enumerate(labels):
            active = label == "1/16"
            svg.rect(gx + col * col_w, ry, col_w - 4, 24, SELECTED if active else HOVER,
                     BORDER_HI if active else BORDER, radius=12)
            svg.text(gx + col * col_w + (col_w - 4) / 2, ry + 16, label, "#ffffff" if active else TEXT, 12, "middle")
    svg.rect(gx, y + 102, 5 * col_w - 4, 24, HOVER, BORDER, radius=12, dash="3 2")
    svg.text(gx + (5 * col_w - 4) / 2, y + 118, "custom…", MUTED, 12, "middle")
    svg.text(x + 12, y + 156, "Space", MUTED, 12)
    segmented(svg, x + 84, y + 140, ["Standard measure", "Authored"], 1)
    # Ties, renamed after what they do, with the count of affected notes.
    svg.text(x + 12, y + 190, "Halfway notes", MUTED, 12)
    svg.text(x + w - 12, y + 190, "2 in selection", AMBER, 11, "end")
    segmented(svg, x + 12, y + 198, ["Round earlier", "Round later", "Block apply"], 0)
    # Holds: whether tails snap too.
    svg.text(x + 12, y + 250, "Hold tails", MUTED, 12)
    svg.text(x + w - 12, y + 250, "3 holds in selection", MUTED, 11, "end")
    segmented(svg, x + 12, y + 258, ["Leave in place", "Snap too"], 1)
    svg.text(x + 12, y + 308, "Presets", MUTED, 12)
    cx = x + 84
    for label in ["Default", "Trills"]:
        cx += chip(svg, cx, y + 292, label) + 4
    chip(svg, cx, y + 292, "+", dashed=True)
    button(svg, x + 12, y + 330, w - 24, "Apply quantization  (Q)", "primary", h=32)
    svg.text(x + 12, y + 384, "field shows before→after ghosts; halfway notes in amber (T-437)", FAINT, 10)
    svg.save("quantize.svg")


def components():
    """Component gallery: one size/radius/spacing system for every new widget (T-436)."""
    svg = Svg(900, 420)
    svg.text(24, 32, "Controls  ·  heights 24 / 28 / 32  ·  radius 4 control, 12 chip, 6 bar, 8 surface  ·  gaps 4 / 8 / 16",
             MUTED, 11)
    y = 56
    svg.text(24, y + 18, "Button", FAINT, 11)
    bx = 110
    for label, kind in [("Primary", "primary"), ("Secondary", "secondary"), ("Ghost", "ghost"),
                        ("Delete…", "destructive")]:
        button(svg, bx, y, 110, label, kind)
        bx += 118
    button(svg, bx, y, 110, "Disabled", disabled=True)
    y += 48
    svg.text(24, y + 18, "Icon", FAINT, 11)
    icon_button(svg, 110, y, "✎")
    icon_button(svg, 142, y, "✎", True)
    icon_button(svg, 174, y, "⋯")
    svg.text(214, y + 18, "28 px · active = accent fill · tooltip always", FAINT, 11)
    y += 48
    svg.text(24, y + 18, "Segmented", FAINT, 11)
    segmented(svg, 110, y, ["One", "Two", "Three"], 1)
    svg.text(330, y + 18, "mutually exclusive choice, 2–4 options", FAINT, 11)
    y += 48
    svg.text(24, y + 18, "Chip", FAINT, 11)
    cx = 110
    cx += chip(svg, cx, y, "1/16", True) + 4
    cx += chip(svg, cx, y, "1/8") + 4
    chip(svg, cx, y, "+", dashed=True)
    svg.text(330, y + 18, "quick value or preset; dashed = adds / opens", FAINT, 11)
    y += 48
    svg.text(24, y + 18, "Checkbox", FAINT, 11)
    for index, state in enumerate(["off", "on", "mixed"]):
        checkbox(svg, 112 + index * 40, y + 5, state)
    svg.text(330, y + 18, "16 px box, whole cell clickable", FAINT, 11)
    y += 48
    svg.text(24, y + 18, "Surfaces", FAINT, 11)
    panel(svg, 110, y, 180, 60)
    svg.text(122, y + 24, "Menu / dialog", TEXT, 12)
    svg.text(122, y + 44, "8 px radius · shadow", FAINT, 11)
    svg.rect(310, y + 12, 260, 38, SURFACE, BORDER_HI, radius=6)
    svg.text(322, y + 36, "Selection bar · 6 px radius", TEXT, 12)
    svg.rect(590, y + 12, 220, 38, SURFACE, BORDER, radius=6)
    svg.text(602, y + 36, "Inspector group · 6 px", TEXT, 12)
    svg.text(24, 400, "State colors: lime accent = a mode or toggle is ON (tools, W/F/C).  Blue = the chosen value, the selection, or the primary action.",
             MUTED, 11)
    svg.save("components.svg")


def field_move():
    svg = Svg(860, 470)
    # Toolbar tools.
    svg.rect(0, 0, 860, 40, SURFACE)
    icon_button(svg, 10, 6, "▸")
    icon_button(svg, 42, 6, "✎", True)
    icon_button(svg, 74, 6, "◈")
    svg.line(110, 8, 110, 32, BORDER)
    svg.text(122, 25, "Select · Draw · Layer   (Alt+1 / Alt+2 / Alt+3)", MUTED, 11)
    # Field.
    fx, fy, fw, fh = 0, 40, 860, 430
    svg.rect(fx, fy, fw, fh, FIELD)
    lane_x, lane_w = 40, 40
    headers = ["#8b3225"] + ["#4a4d5c" if index % 2 == 0 else "#1e1f28" for index in range(7)]
    for index in range(8):
        body = "#2a273d" if index == 0 else ("#252d3b" if index % 2 else "#212835")
        svg.rect(lane_x + index * lane_w, fy, lane_w, fh, body)
        # Lane header numbers: white with a black outline on any header color (T-387).
        svg.rect(lane_x + index * lane_w, fy, lane_w, 20, headers[index])
        svg.text(lane_x + index * lane_w + 5, fy + 15, str(index + 1), "#f0f4fa", 12, outline="#000000")
    bgm_x = lane_x + 8 * lane_w + 24
    for index in range(4):
        svg.rect(bgm_x + index * 44, fy, 44, fh, "#1d2129" if index % 2 else "#1a1e26")
        svg.text(bgm_x + index * 44 + 22, fy + 15, f"B{index + 1}", MUTED, 11, "middle")
    for row in range(0, 11):
        y = fy + 30 + row * 40
        svg.line(lane_x, y, bgm_x + 176, y, "#4b5568" if row % 4 == 0 else GRID, 1.5 if row % 4 == 0 else 1)

    def note(lane, row, fill, label=None, stroke=None, dash=None, opacity=1.0, x=None, width=None):
        nx = x if x is not None else lane_x + lane * lane_w + 3
        ny = fy + 30 + row * 40 - 6
        nw = width or lane_w - 6
        svg.rect(nx, ny, nw, 12, fill, stroke, radius=2, width=2 if stroke else 1, dash=dash, opacity=opacity)
        if label:
            svg.text(nx + nw / 2, ny + 10, label, "#ffffff", 9, "middle", outline="#000000")
    # Existing notes.
    note(6, 1, "#71b8da", "0A")
    note(4, 4, "#71b8da", "0B")
    note(5, 7, "#b1ce81", "1C")
    # Selected originals (dimmed while dragging) and their ghosts 4 rows up, same shape.
    for lane, row, label in [(1, 6, "05"), (3, 7, "06"), (4, 8, "07")]:
        note(lane, row, "#71b8da", label, opacity=0.35)
    note(1, 2, "none", "05", stroke=BORDER_HI, dash="4 2")
    note(3, 3, "none", "06", stroke=BORDER_HI, dash="4 2")
    # The third ghost lands on 0B: red ghost, occupant outlined.
    note(4, 4, CORAL, "07", stroke=CORAL, opacity=0.75)
    svg.rect(lane_x + 4 * lane_w + 1, fy + 30 + 4 * 40 - 8, lane_w - 2, 16, "none", CORAL, radius=3, width=2)
    # Amber "will move to BGM" destination when overwrite policy is on (T-421).
    note(0, 4, AMBER, "0B", stroke=AMBER, dash="3 2", opacity=0.7, x=bgm_x + 3, width=38)
    svg.path(f"M{lane_x + 5 * lane_w - 2} {fy + 30 + 4 * 40} C {bgm_x - 30} {fy + 175}, {bgm_x - 20} {fy + 185}, "
             f"{bgm_x + 2} {fy + 30 + 4 * 40}", stroke=AMBER, width=1.2)
    # Tool chip (T-414).
    panel(svg, 46, fy + 362, 330, 30)
    svg.text(58, fy + 382, "✎ Draw · 0A kick.wav · right-drag erases", TEXT, 12)
    # Status line.
    svg.rect(0, 446, 860, 24, "#1e1f28")
    svg.text(10, 462, "1 note would overlap — release cancels   ·   overwrite policy: 0B → BGM lane B1", "#ff9e86", 11)
    # Legend.
    lx = bgm_x + 196
    for index, (color, stroke, dash, label) in enumerate([
            ("none", BORDER_HI, "4 2", "ghost: valid target"),
            (CORAL, CORAL, None, "ghost: collides (T-420)"),
            ("none", CORAL, None, "occupant outlined"),
            (AMBER, AMBER, "3 2", "displaced to BGM (T-421)")]):
        svg.rect(lx, fy + 50 + index * 26, 26, 12, color, stroke, radius=2, width=2, dash=dash,
                 opacity=0.7 if color != "none" else None)
        svg.text(lx + 34, fy + 60 + index * 26, label, MUTED, 11)
    svg.save("field-move-collision.svg")


def markers():
    svg = Svg(380, 300)
    x, y, w = 12, 12, 356

    def group(top, title, rows):
        svg.text(x + 8, top + 20, title, BRIGHT, 13, weight="bold")  # Same 8 px margin as the buttons on the right.
        bx = x + w - 8 - (4 * 30 - 4)  # Right edge flush with the row highlight.
        for glyph in ["‹", "›", "+", ""]:
            icon_button(svg, bx, top + 2, glyph, size=26)
            if not glyph:  # Drawn trash can.
                tx, ty = bx + 8, top + 9
                svg.line(tx - 1, ty + 2, tx + 11, ty + 2, TEXT, 1.4)
                svg.line(tx + 3.5, ty, tx + 6.5, ty, TEXT, 1.4)
                svg.path(f"M{tx + 1} {ty + 4} L{tx + 2} {ty + 13} L{tx + 8} {ty + 13} L{tx + 9} {ty + 4}",
                         stroke=TEXT, width=1.4)
            bx += 30
        for index, (label, position, selected) in enumerate(rows):
            ry = top + 36 + index * 26
            if selected:
                svg.rect(x + 8, ry - 2, w - 16, 24, "#172e60", radius=4)
            svg.text(x + 16, ry + 15, label, TEXT, 12)
            svg.text(x + w - 16, ry + 15, position, MUTED, 12, "end")

    svg.rect(x, y, w, 276, SURFACE, BORDER, radius=6)
    group(y + 8, "Bookmarks", [("drop", "m17 · 0:42.100", True), ("break", "m33 · 1:21.900", False),
                                ("outro", "m61 · 2:18.000", False)])
    svg.line(x + 8, y + 132, x + w - 8, y + 132, BORDER)
    group(y + 140, "Sections", [("verse 1", "m1-m16", False), ("chorus", "m17-m32", False),
                                ("bridge", "m33-m40", False)])
    svg.note(x, 298, "")
    svg.save("bookmarks-sections.svg")


def hitsound_palette():
    svg = Svg(380, 440)
    svg.rect(0, 0, 44, 440, SURFACE)
    y = 8
    for glyph, active in [("●", True), ("▮", False), ("◆", False), ("✱", False)]:
        icon_button(svg, 7, y, glyph, active, 30)
        y += 36
    svg.line(8, y + 2, 36, y + 2, BORDER)
    y += 10
    svg.text(22, y + 10, "HS", FAINT, 9, "middle")
    y += 16
    for letter, state in [("W", "on"), ("F", "off"), ("C", "mixed")]:
        fill = ACCENT if state == "on" else HOVER
        svg.rect(7, y, 30, 30, fill, None if state == "on" else BORDER, radius=4)
        svg.text(22, y + 19, letter, "#12131a" if state == "on" else TEXT, 13, "middle", "bold")
        if state == "mixed":  # Mixed selection: letter plus a dash under it.
            svg.line(15, y + 25, 29, y + 25, ACCENT, 2)
        y += 36
    y += 6
    for letter, title in [("N", "normal"), ("S", "addition")]:
        svg.rect(7, y, 30, 30, HOVER, BORDER_HI, radius=4)
        svg.text(22, y + 20, letter, BRIGHT, 13, "middle", "bold")
        svg.text(52, y + 14, f"{title} set: Auto/Normal/Soft/Drum", MUTED, 10)
        svg.text(52, y + 27, "click cycles · right-click picks", MUTED, 10)
        y += 36
    svg.text(52, 186, "Whistle on for all selected", MUTED, 10)
    svg.text(52, 222, "Finish off", MUTED, 10)
    svg.text(52, 258, "Clap mixed (dash under letter)", MUTED, 10)
    svg.text(52, 30, "note kinds (existing palette)", MUTED, 10)
    svg.text(52, 420, "W/E/R toggle · Shift/Alt+Q/W/E/R set the sets", MUTED, 10)
    svg.save("hitsound-palette.svg")


# Lane strips: S scratch, 1 white key, 2 black key, P pedal, F free zone, space = stage gap.
LANE_FILL = {"S": "#8b3225", "1": "#4a4d5c", "2": "#262833", "P": "#8a6a2c", "F": "#3b4a3a"}


def lane_strip(svg, x, y, spec, lane_w, h, labels=None, gap=None):
    """Mini lane diagram; returns the strip width."""
    gap = gap if gap is not None else lane_w
    cursor = x
    index = 0
    for kind in spec:
        if kind == " ":
            cursor += gap
            continue
        width = lane_w * 1.6 if kind == "S" else lane_w
        svg.rect(cursor, y, width - 1, h, LANE_FILL[kind], radius=1)
        if labels:
            svg.text(cursor + (width - 1) / 2, y + h - 6, labels[index], "#f0f4fa", 10, "middle",
                     outline="#000000")
        cursor += width
        index += 1
    return cursor - x


def link_icon(svg, x, y, color, broken=False):
    """Two chain links, 14 x 8; broken separates them with a slash."""
    if not broken:
        svg.rect(x, y, 8, 6, "none", color, radius=3, width=1.4)
        svg.rect(x + 6, y + 2, 8, 6, "none", color, radius=3, width=1.4)
        return
    svg.rect(x - 2, y + 1, 7, 6, "none", color, radius=3, width=1.4)
    svg.rect(x + 9, y + 1, 7, 6, "none", color, radius=3, width=1.4)
    svg.line(x + 6, y + 9, x + 8, y - 1, color, 1.4)


def new_project_layout():
    """Proposal: game list + layout cards + variant toggles instead of one 60-entry combo."""
    svg = Svg(980, 720, "#0b0c11")
    x, y, w, h = 30, 24, 920, 548
    panel(svg, x, y, w, h)
    svg.text(x + 16, y + 30, "Create a new project", BRIGHT, 14, weight="bold")
    svg.text(x + w - 22, y + 30, "✕", MUTED, 14, "middle")
    # Search filters every game's cards; typing "14" or "pedal" jumps straight to matches.
    svg.rect(x + 200, y + 46, w - 216, 28, BG, BORDER, radius=4)
    svg.text(x + 212, y + 65, "⌕  Search layouts  —  14k, pedal, pump, special…", FAINT, 12)

    # Game list.
    games = [("Recent", "3"), ("osu!mania", "20"), ("BMS / BME", "28"), ("PMS", "4"),
             ("StepMania", "3"), ("BMSON", "2")]
    svg.text(x + 16, y + 64, "Game", MUTED, 12)
    for index, (name, count) in enumerate(games):
        gy = y + 84 + index * 32
        if name == "BMS / BME":
            svg.rect(x + 12, gy, 172, 28, "#172e60", radius=4)
            svg.rect(x + 12, gy, 3, 28, BORDER_HI, radius=1)
        svg.text(x + 24, gy + 19, name, BRIGHT if name == "BMS / BME" else TEXT, 13)
        svg.text(x + 176, gy + 19, count, FAINT, 11, "end")
    svg.line(x + 192, y + 46, x + 192, y + h - 60, BORDER)

    # Layout cards: one per lane shape; variants are toggles below instead of separate entries.
    cards = [("BMS 5K", "S12121", "5 keys + scratch"),
             ("BME 7K", "S1212121", "7 keys + scratch"),
             ("DP5 / 10K", "S12121 12121S", "two 5K sides"),
             ("DP7 / 14K", "S1212121 1212121S", "two 7K sides"),
             ("10K no scratch", "12121 12121", "two 5K, no scratch"),
             ("Double scratch", "SS1212121", "FPP"),
             ("Octave", "P1212121212121", "foot pedal"),
             ("Project2DX 7K", "S1212121", "7 keys + scratch")]
    gx, gy0, cw, ch = x + 208, y + 88, 168, 104
    for index, (name, spec, hint) in enumerate(cards):
        cx = gx + (index % 4) * (cw + 8)
        cy = gy0 + (index // 4) * (ch + 8)
        active = name == "BME 7K"
        svg.rect(cx, cy, cw, ch, "#172e60" if active else HOVER, BORDER_HI if active else BORDER,
                 radius=6, width=2 if active else 1)
        lanes = [c for c in spec if c != " "]
        lane_w = min(12, 120 / (len(lanes) + 2))
        strip_w = sum(lane_w * (1.6 if c == "S" else 1) for c in lanes) + spec.count(" ") * lane_w
        lane_strip(svg, cx + (cw - strip_w) / 2, cy + 12, spec, lane_w, 46)
        svg.text(cx + 10, cy + 78, name, BRIGHT if active else TEXT, 13, weight="bold" if active else "normal")
        svg.text(cx + 10, cy + 94, hint, MUTED, 11)

    # Variant toggles for the selected card. Only the options that card has are shown.
    vy = gy0 + 2 * (ch + 8) + 10
    svg.line(gx, vy - 4, x + w - 16, vy - 4, BORDER)
    svg.text(gx, vy + 20, "Scratch", MUTED, 12)
    segmented(svg, gx + 110, vy + 4, ["Outside (left)", "Inside (right)"], 0)
    svg.text(gx, vy + 54, "Extra lanes", MUTED, 12)
    segmented(svg, gx + 110, vy + 38, ["None", "Foot pedal"], 0)
    svg.text(gx + 396, vy + 54, "Free zone appears only for 5K / DP5", FAINT, 11)

    # Large preview with the lanes Compose will open with (and their default keys when bound).
    py = vy + 76
    svg.rect(gx, py, x + w - 16 - gx, 92, FIELD, BORDER, radius=6)
    svg.text(gx + 12, py + 20, "Preview", MUTED, 12)
    lane_strip(svg, gx + 12, py + 30, "S1212121", 34, 50, ["SC", "1", "2", "3", "4", "5", "6", "7"])
    svg.text(gx + 330, py + 46, "8 lanes · BMS lane profile BME 7K", TEXT, 12)
    svg.text(gx + 330, py + 64, "Saves as  .bme   ·  default export: BMS", TEXT, 12)
    svg.text(gx + 330, py + 80, "Recent remembers the last 3 choices", FAINT, 11)

    # Footer.
    svg.line(x + 16, y + h - 52, x + w - 16, y + h - 52, BORDER)
    svg.text(x + 16, y + h - 26, "Enter creates · ↑↓ games · ←→ cards · Tab to variants", FAINT, 11)
    button(svg, x + w - 16 - 140 - 8 - 84, y + h - 40, 84, "Cancel")
    button(svg, x + w - 16 - 140, y + h - 40, 140, "Create project", "primary")

    # osu!mania page inset: key counts are chips, not 20 cards.
    iy = y + h + 18
    svg.rect(x, iy, w, 64, SURFACE, BORDER, radius=6)
    svg.text(x + 16, iy + 22, "osu!mania page", MUTED, 12)
    svg.text(x + 16, iy + 48, "Keys", MUTED, 12)
    cx = x + 66
    for keys in range(1, 19):
        cx += chip(svg, cx, iy + 32, str(keys), active=keys == 7) + 4
    svg.text(cx + 12, iy + 48, "SpecialStyle", TEXT, 12)
    checkbox(svg, cx + 116, iy + 36, "off")
    svg.text(cx + 140, iy + 48, "(12K, 16K)", FAINT, 11)
    svg.note(x, iy + 86, "Import audio as a new project: same dialog, with the audio path under the title and Recent selected.")
    svg.note(x, iy + 102, "The catalog stays one table (lane_profiles.cpp); cards group its entries by lane shape and variants pick the id.")
    svg.save("new-project-layout.svg")


def timing_links():
    """T-720 proposal: per-category timing links, copy-on-write overrides and relink."""
    svg = Svg(1000, 620)
    LINK = "#86b4ff"
    # Compose lane headers with link badges.
    svg.text(16, 24, "Compose · timing lane headers", MUTED, 12)
    hx, hy = 16, 36
    lanes = [("BPM", "#e8a24a", "linked"), ("SV", "#b1ce81", "local"), ("SPD", "#71b8da", "linked"),
             ("STP", "#ef6b6b", "linked"), ("DLY", "#e8a24a", "linked"), ("WRP", "#c58af0", None),
             ("KIA", "#f06aa0", "own")]
    lane_w = 60
    svg.rect(hx, hy, lane_w * len(lanes) + 4 * 40, 300, FIELD)
    for index, (label, color, state) in enumerate(lanes):
        lx = hx + index * lane_w
        svg.rect(lx, hy, lane_w - 1, 300, "#1d2129" if index % 2 else "#1a1e26")
        svg.text(lx + 6, hy + 18, label, color, 12)
        if state == "linked":
            link_icon(svg, lx + 40, hy + 8, LINK)
        elif state == "local":
            link_icon(svg, lx + 40, hy + 8, AMBER, broken=True)
        if state == "linked":  # Linked events are drawn from the shared source: dashed outline.
            svg.rect(lx + 2, hy + 24, lane_w - 5, 276, "none", LINK, radius=2, dash="2 4", opacity=0.45)
    for index in range(4):
        svg.rect(hx + lane_w * len(lanes) + index * 40, hy, 39, 300, "#252d3b" if index % 2 else "#212835")
        svg.text(hx + lane_w * len(lanes) + index * 40 + 6, hy + 18, str(index + 1), "#f0f4fa", 12, outline="#000000")
    for row in range(1, 7):
        svg.line(hx, hy + 22 + row * 46, hx + lane_w * len(lanes) + 160, hy + 22 + row * 46, GRID)
    # Events.
    for lane, row, value in [(0, 1, "174"), (0, 4, "87"), (1, 2, "1.5×"), (1, 3, "0.8×"), (1, 5, "1×"),
                             (3, 4, "1/8"), (6, 2, "on")]:
        svg.rect(hx + lane * lane_w + 4, hy + 22 + row * 46 - 8, lane_w - 9, 16, HOVER,
                 LINK if lanes[lane][2] == "linked" else (AMBER if lane == 1 else BORDER), radius=3)
        svg.text(hx + lane * lane_w + 10, hy + 22 + row * 46 + 4, value, TEXT, 11)
    # Hover tooltip on a linked BPM event.
    panel(svg, hx + 66, hy + 60, 310, 46)
    svg.text(hx + 78, hy + 80, "BPM 174 · from “Song timing”", TEXT, 12)
    svg.text(hx + 78, hy + 97, "editing here detaches BPM for this chart", FAINT, 11)

    # Inspector: chart timing links.
    ix, iy, iw = 690, 36, 294
    svg.rect(ix, iy, iw, 424, SURFACE, BORDER, radius=6)
    svg.text(ix + 12, iy + 24, "Timing links", BRIGHT, 13, weight="bold")
    svg.text(ix + 12, iy + 50, "Source", MUTED, 12)
    svg.rect(ix + 70, iy + 34, iw - 82, 26, HOVER, BORDER, radius=4)
    svg.text(ix + 80, iy + 51, "Song timing (shared)", TEXT, 12)
    svg.text(ix + iw - 22, iy + 51, "▾", MUTED, 12, "middle")
    rows = [("BPM", "linked"), ("SV / scroll", "local"), ("SPEEDS", "linked"), ("Stops", "linked"),
            ("Delays", "linked"), ("Kiai", "own")]
    for index, (label, state) in enumerate(rows):
        ry = iy + 74 + index * 34
        if state == "local":
            svg.rect(ix + 6, ry - 2, iw - 12, 32, "#2d2616", radius=4)
        svg.text(ix + 14, ry + 18, label, TEXT, 12)
        if state == "linked":
            link_icon(svg, ix + 110, ry + 9, LINK)
            svg.text(ix + 130, ry + 18, "Linked", LINK, 12)
            button(svg, ix + iw - 88, ry + 2, 76, "Detach", "ghost", h=24)
        elif state == "local":
            link_icon(svg, ix + 110, ry + 9, AMBER, broken=True)
            svg.text(ix + 130, ry + 18, "Local", AMBER, 12)
            button(svg, ix + iw - 88, ry + 2, 76, "Relink…", h=24)
        else:
            svg.text(ix + 130, ry + 18, "Not linked", FAINT, 12)
            button(svg, ix + iw - 88, ry + 2, 76, "Link", h=24)
    svg.line(ix + 12, iy + 284, ix + iw - 12, iy + 284, BORDER)
    svg.text(ix + 12, iy + 306, "Used by 3 charts: 7K Hyper, 7K Another,", MUTED, 11)
    svg.text(ix + 12, iy + 322, "14K (SV local in 7K Another)", MUTED, 11)
    button(svg, ix + 12, iy + 336, iw - 24, "Edit shared timing…", h=28)
    button(svg, ix + 12, iy + 372, iw - 24, "Link all categories", "ghost", h=28)

    # First-write toast.
    ty = 356
    svg.rect(16, ty, 580, 48, SURFACE, AMBER, radius=6)
    link_icon(svg, 30, ty + 16, AMBER, broken=True)
    svg.text(54, ty + 21, "SV is now local to 7K Another.", BRIGHT, 12)
    svg.text(54, ty + 38, "Other charts keep the shared SV; BPM and the rest stay linked.", MUTED, 11)
    svg.text(580, ty + 21, "Undo", LINK, 12, "end")
    svg.text(580, ty + 38, "Relink", LINK, 12, "end")

    # Relink confirmation.
    dx, dy, dw, dh = 16, 422, 580, 150
    panel(svg, dx, dy, dw, dh)
    svg.text(dx + 16, dy + 28, "Relink SV to Song timing?", BRIGHT, 14, weight="bold")
    svg.text(dx + 16, dy + 54, "This chart's 14 local SV events are replaced by the shared 9.", TEXT, 12)
    svg.text(dx + 16, dy + 72, "Only SV changes; Undo restores the local events.", MUTED, 12)
    button(svg, dx + 16, dy + dh - 44, 150, "Copy local to new set", "ghost")
    button(svg, dx + dw - 16 - 196 - 8 - 84, dy + dh - 44, 84, "Cancel")
    button(svg, dx + dw - 16 - 196, dy + dh - 44, 196, "Relink, discard 14", "destructive")

    for index, line in enumerate([
            "blue chain: follows the shared source",
            "amber broken chain: local override",
            "dashed blue band: linked lane; editing",
            "  is allowed and detaches on first write",
            "WRP: no badge; extra categories",
            "  are an open T-720 question",
            "Source: assumes named shared sets,",
            "  one answer to T-720's first question"]):
        svg.note(ix, 478 + index * 14, line)
    svg.note(16, 606, "Detach keeps the current events as a local copy. Relink is the only step that can lose data, so only Relink confirms.")
    svg.save("timing-links.svg")


def output_groups():
    """Proposal: export targets as cards with one Export action and an editor dialog."""
    svg = Svg(1100, 600)
    x0, w0 = 16, 600
    svg.text(x0, 30, "Exports", BRIGHT, 16, weight="bold")
    svg.text(x0 + 76, 30, "3", MUTED, 13)
    button(svg, x0 + w0 - 220, 12, 110, "Export all")
    button(svg, x0 + w0 - 102, 12, 102, "+ New", "primary")
    formats = {"BMS": "#8b3225", "osu!": "#c0558f", "SSC": "#3f7fbf"}
    targets = [
        ("BMS", "BMS release", "exports/bms/", ["7K Hyper", "7K Another", "14K"],
         "Exported 10:42 · 3 charts · 12 sounds copied", "#78d296", True),
        ("osu!", "osu!mania", "exports/osu/", ["7K Hyper", "7K Another"],
         "Not exported yet", MUTED, False),
        ("SSC", "StepMania", "exports/sm/song.ssc", ["7K Hyper"],
         "Exported 09:15 · 1 conversion warning — view", AMBER, False),
    ]
    y = 46
    for fmt, name, dest, charts, status, status_color, expanded in targets:
        h = 160 if expanded else 86
        svg.rect(x0, y, w0, h, SURFACE, BORDER_HI if expanded else BORDER, radius=6)
        svg.rect(x0 + 12, y + 12, 44, 22, formats[fmt], radius=4)
        svg.text(x0 + 34, y + 27, fmt, "#ffffff", 11, "middle", "bold")
        svg.text(x0 + 66, y + 28, name, BRIGHT, 13, weight="bold")
        svg.text(x0 + 66 + len(name) * 8 + 12, y + 28, "→ " + dest, MUTED, 12)
        # Split button: Export ▾ (Replace existing / Keep existing / Choose folder…).
        svg.rect(x0 + w0 - 140, y + 10, 92, 28, SELECTED, radius=4)
        svg.text(x0 + w0 - 108, y + 29, "Export", "#ffffff", 12, "middle")
        svg.line(x0 + w0 - 72, y + 14, x0 + w0 - 72, y + 34, "#5b8ff0")
        svg.text(x0 + w0 - 60, y + 29, "▾", "#ffffff", 12, "middle")
        icon_button(svg, x0 + w0 - 40, y + 10, "⋯")
        cx = x0 + 66
        for chart in charts:
            cx += chip(svg, cx, y + 44, chart, h=22) + 4
        chip(svg, cx, y + 44, "+", h=22, dashed=True)
        svg.text(x0 + 66, y + 80, status, status_color, 11)
        if expanded:  # Preview: what Export will write, before writing it.
            svg.line(x0 + 12, y + 92, x0 + w0 - 12, y + 92, BORDER)
            files = [("exports/bms/7k_hyper.bme", "replace", MUTED),
                     ("exports/bms/7k_another.bme", "replace", MUTED),
                     ("exports/bms/14k.bme", "new", "#78d296"),
                     ("exports/bms/  12 sounds, 1 image", "copy", MUTED)]
            for index, (path, verb, color) in enumerate(files):
                svg.text(x0 + 24, y + 110 + index * 14, path, TEXT, 11)
                svg.text(x0 + w0 - 24, y + 110 + index * 14, verb, color, 11, "end")
        y += h + 10

    # Editor dialog.
    dx, dy, dw, dh = 640, 46, 444, 520
    panel(svg, dx, dy, dw, dh)
    svg.text(dx + 16, dy + 30, "Edit export · StepMania", BRIGHT, 14, weight="bold")
    svg.text(dx + 16, dy + 62, "Name", MUTED, 12)
    svg.rect(dx + 120, dy + 46, dw - 136, 26, BG, BORDER, radius=4)
    svg.text(dx + 130, dy + 63, "StepMania", TEXT, 12)
    svg.text(dx + 16, dy + 96, "Format", MUTED, 12)
    segmented(svg, dx + 120, dy + 80, ["BMS", "BMSON", "osu!", "StepMania"], 3)
    # Format options live with the format, so the global StepMania policy section goes away.
    svg.rect(dx + 16, dy + 114, dw - 32, 112, HOVER, BORDER, radius=6)
    svg.text(dx + 28, dy + 136, "File", MUTED, 12)
    segmented(svg, dx + 150, dy + 120, ["SM", "SSC"], 1, seg_w=56)
    svg.text(dx + 28, dy + 170, "Warps as", MUTED, 12)
    segmented(svg, dx + 150, dy + 154, ["WARPS", "neg. BPM", "neg. STOP"], 0)
    svg.text(dx + 28, dy + 204, "Check for", MUTED, 12)
    segmented(svg, dx + 150, dy + 188, ["Syntax", "SM 5.1", "Etterna"], 1)
    svg.text(dx + 16, dy + 254, "Save to", MUTED, 12)
    svg.rect(dx + 120, dy + 238, dw - 220, 26, BG, BORDER, radius=4)
    svg.text(dx + 130, dy + 255, "exports/sm/song.ssc", TEXT, 12)
    button(svg, dx + dw - 92, dy + 238, 76, "Browse…", h=26)
    svg.text(dx + 120, dy + 280, "One .ssc holds every chart", FAINT, 11)
    svg.text(dx + 16, dy + 310, "Charts", MUTED, 12)
    members = [("7K Hyper", "on", "kb7", MUTED),
               ("7K Another", "off", "", MUTED),
               ("14K", "off", "no 14-lane step type", AMBER)]
    for index, (name, state, hint, color) in enumerate(members):
        my = dy + 322 + index * 28
        checkbox(svg, dx + 120, my + 4, state)
        svg.text(dx + 144, my + 17, name, TEXT if color != AMBER else FAINT, 12)
        if hint:
            svg.text(dx + 240, my + 17, hint, color, 11)
    svg.text(dx + 16, dy + 426, "Existing", MUTED, 12)
    segmented(svg, dx + 120, dy + 410, ["Ask", "Replace", "Keep"], 0)
    svg.line(dx + 16, dy + dh - 56, dx + dw - 16, dy + dh - 56, BORDER)
    button(svg, dx + 16, dy + dh - 44, 84, "Delete…", "destructive")
    button(svg, dx + dw - 16 - 84 - 8 - 84, dy + dh - 44, 84, "Cancel")
    button(svg, dx + dw - 16 - 84, dy + dh - 44, 84, "Save", "primary")
    svg.note(x0, y + 6, "one card per target · chips add/remove charts · ⋯: Edit, Preview, Delete…")
    svg.note(x0, y + 22, "Export ▾: Replace existing / Keep existing — no second Export… dialog")
    svg.save("output-groups.svg")


if __name__ == "__main__":
    unsaved_changes()
    tap_timing()
    chart_table()
    quantize()
    components()
    field_move()
    markers()
    hitsound_palette()
    new_project_layout()
    timing_links()
    output_groups()
