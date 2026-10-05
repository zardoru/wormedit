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
        svg.text(x + 16, top + 20, title, BRIGHT, 13, weight="bold")
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
    group(y + 140, "Sections", [("verse 1", "m1–m16", False), ("chorus", "m17–m32", False),
                                ("bridge", "m33–m40", False)])
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


if __name__ == "__main__":
    unsaved_changes()
    tap_timing()
    chart_table()
    quantize()
    components()
    field_move()
    markers()
    hitsound_palette()
