#!/usr/bin/env python3
"""Checks that DESIGN.md matches src/ui/theme.h and src/ui/layout.h.

Compares the colours (the rgb() arguments of cv::ui::color), the screen geometry and the glyph sizes.
Standard library only.
    usage: tools/check_design_sync.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# DESIGN.md token -> constant it must equal in layout.h, and the dimension it is written as.
GEOMETRY = {
    "kW": ("components", "screen", "width"),
    "kH": ("components", "screen", "height"),
    "kHeaderH": ("components", "header", "height"),
    "kFooterH": ("components", "footer", "height"),
}
# theme.h colour name -> DESIGN.md colour name when they differ.
ALIASES = {"primary": "accent"}


def front_matter(text):
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    if not m:
        sys.exit("DESIGN.md has no front matter")
    return m.group(1)


def yaml_section(fm, key):
    """Lines of the top-level `key:` block, as {indent-2 name: value or sub-lines}."""
    m = re.search(rf"^{key}:\n((?:[ ]+.*\n?)+)", fm + "\n", re.M)
    if not m:
        sys.exit(f"DESIGN.md front matter has no '{key}:' block")
    return m.group(1)


def theme_colors(theme):
    ns = re.search(r"namespace color \{(.*?)\}  // namespace color", theme, re.S)
    if not ns:
        sys.exit("theme.h: namespace color not found")
    out = {}
    for name, r, g, b in re.findall(
        r"constexpr uint16_t (\w+) = rgb\((\d+), (\d+), (\d+)\);", ns.group(1)
    ):
        out[name] = "#%02X%02X%02X" % (int(r), int(g), int(b))
    return out


def layout_consts(layout):
    return {n: int(v) for n, v in re.findall(r"constexpr int (\w+) = (\d+);", layout)}


def component_dim(fm, comp, prop):
    block = yaml_section(fm, "components")
    m = re.search(rf"^  {re.escape(comp)}:\n((?:    .*\n)+)", block, re.M)
    if not m:
        return None
    d = re.search(rf"^    {prop}: (\d+)px", m.group(1), re.M)
    return int(d.group(1)) if d else None


def main():
    fm = front_matter((ROOT / "DESIGN.md").read_text())
    theme = (ROOT / "src/ui/theme.h").read_text()
    layout = (ROOT / "src/ui/layout.h").read_text()
    errors = []

    doc_colors = {
        k: v.upper()
        for k, v in re.findall(r'^  (\w+): "(#[0-9A-Fa-f]{6})"', yaml_section(fm, "colors"), re.M)
    }
    src_colors = theme_colors(theme)
    for name, hexv in src_colors.items():
        if doc_colors.get(name) != hexv:
            errors.append(f"colour {name}: theme.h {hexv}, DESIGN.md {doc_colors.get(name)}")
    for name in doc_colors:
        if name not in src_colors and name not in ALIASES:
            errors.append(f"colour {name} is in DESIGN.md but not in theme.h")
    for alias, target in ALIASES.items():
        if doc_colors.get(alias) != src_colors.get(target):
            errors.append(f"colour {alias} must equal {target} ({src_colors.get(target)})")

    consts = layout_consts(layout)
    for const, (_, comp, prop) in GEOMETRY.items():
        got = component_dim(fm, comp, prop)
        if got != consts.get(const):
            errors.append(f"{const}: layout.h {consts.get(const)}, DESIGN.md {comp}.{prop} {got}")

    # Glyph cells are quoted in the prose of the Typography section.
    text = (ROOT / "DESIGN.md").read_text()
    for w, h, label in (("kMonoW", "kMonoH", "mono"), ("kSmallW", "kSmallH", "small")):
        cell = f"{consts[w]}x{consts[h]}"
        if not re.search(rf"\*\*{label}\*\*[^\n]*\b{cell}\b", text):
            errors.append(f"Typography section does not quote the {label} cell as {cell}")
    body = f"{consts['kHeaderH'] + 2}"  # kBodyY
    if not re.search(rf"y = {body} \(`kBodyY`\)", text):
        errors.append(f"Layout section does not say the body starts at y = {body} (kBodyY)")
    kbody_h = consts["kH"] - consts["kHeaderH"] - consts["kFooterH"] - 3
    if not re.search(rf"for {kbody_h} px \(`kBodyH`\)", text):
        errors.append(f"Layout section does not say the body is {kbody_h} px (kBodyH)")

    if errors:
        print("DESIGN.md is out of sync with src/ui/theme.h and layout.h:", file=sys.stderr)
        for e in errors:
            print("  -", e, file=sys.stderr)
        return 1
    print(f"DESIGN.md matches theme.h and layout.h ({len(src_colors)} colours, geometry, glyph cells)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
