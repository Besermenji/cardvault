#!/usr/bin/env python3
"""Checks that DESIGN.md matches src/ui/theme.h and src/ui/layout.h.

Compares every colour of cv::ui::color, the font names, the screen geometry (kW, kH, kHeaderH,
kFooterH, kBodyY, kBodyH, as layout.h computes them) and the glyph cells. Component sizes and radii
are kept by hand. Standard library only.
    usage: tools/check_design_sync.py
"""
import ast
import operator
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# layout.h constant -> (component, property) in the DESIGN.md front matter, written as <n>px.
GEOMETRY = {
    "kW": ("screen", "width"),
    "kH": ("screen", "height"),
    "kHeaderH": ("header", "height"),
    "kFooterH": ("footer", "height"),
}
# DESIGN.md colour name -> theme.h colour it must equal.
ALIASES = {"primary": "accent"}
# theme.h font macro -> DESIGN.md typography token.
FONTS = {"CV_FONT_MONO": "mono", "CV_FONT_SMALL": "small", "CV_FONT_TITLE": "title", "CV_FONT_BIG": "big"}


class SyncError(Exception):
    pass


def front_matter(text):
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    if not m:
        raise SyncError("DESIGN.md has no front matter")
    return m.group(1)


def yaml_block(text, key, indent):
    """Body of the `key:` mapping at `indent` spaces, up to the next line indented no deeper."""
    pad = " " * indent
    m = re.search(rf"^{pad}{re.escape(key)}:\n((?:{pad}[ ]+.*\n?)+)", text + "\n", re.M)
    if not m:
        raise SyncError(f"DESIGN.md front matter has no '{key}:' block")
    return m.group(1)


def theme_colors(theme, errors):
    # The colour namespace holds only constants, so it ends at the first closing brace.
    ns = re.search(r"namespace\s+color\s*\{([^{}]*)\}", theme)
    if not ns:
        raise SyncError("theme.h: namespace color { ... } not found")
    out = {}
    for name, value in re.findall(r"constexpr\s+uint16_t\s+(\w+)\s*=\s*([^;]+);", ns.group(1)):
        m = re.fullmatch(r"rgb\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)", value.strip())
        try:
            r, g, b = (int(x, 0) for x in m.groups()) if m else (None,) * 3
        except ValueError:
            m = None
        if not m:
            errors.append(f"theme.h colour {name} = {value.strip()}: write it as rgb(r, g, b) so it can be checked")
            continue
        out[name] = "#%02X%02X%02X" % (r, g, b)
    if not out:
        raise SyncError("theme.h: no colours found in namespace color")
    return out


def theme_fonts(theme):
    return dict(re.findall(r"#define\s+(CV_FONT_\w+)\s+\(&fonts::(\w+)\)", theme))


# C++ integer division truncates toward zero.
_OPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul, ast.Div: lambda a, b: int(a / b)}


def _eval(node, env):
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return node.value
    if isinstance(node, ast.Name) and node.id in env:
        return env[node.id]
    if isinstance(node, ast.BinOp) and type(node.op) in _OPS:
        return _OPS[type(node.op)](_eval(node.left, env), _eval(node.right, env))
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -_eval(node.operand, env)
    raise ValueError


def layout_consts(layout, errors):
    """constexpr int values of layout.h, evaluating integer expressions of earlier constants like C++ does."""
    env = {}
    for name, expr in re.findall(r"constexpr\s+int\s+(\w+)\s*=\s*([^;]+);", layout):
        try:
            env[name] = _eval(ast.parse(expr.strip(), mode="eval").body, env)
        except (SyntaxError, ValueError):
            errors.append(f"layout.h {name} = {expr.strip()}: cannot evaluate it")
    return env


def main():
    text = (ROOT / "DESIGN.md").read_text()
    theme = (ROOT / "src/ui/theme.h").read_text()
    layout = (ROOT / "src/ui/layout.h").read_text()
    errors = []
    try:
        fm = front_matter(text)
        components = yaml_block(fm, "components", 0)
        typography = yaml_block(fm, "typography", 0)

        # Colours: every theme.h colour, nothing extra, aliases equal their target.
        doc_colors = {k: v.upper() for k, v in
                      re.findall(r'^  (\w+): "(#[0-9A-Fa-f]{6})"', yaml_block(fm, "colors", 0), re.M)}
        src_colors = theme_colors(theme, errors)
        for name, hexv in src_colors.items():
            if doc_colors.get(name) != hexv:
                errors.append(f"colour {name}: theme.h {hexv}, DESIGN.md {doc_colors.get(name)}")
        for name in doc_colors:
            if name not in src_colors and name not in ALIASES:
                errors.append(f"colour {name} is in DESIGN.md but not in theme.h")
        for alias, target in ALIASES.items():
            if target in src_colors and doc_colors.get(alias) != src_colors[target]:
                errors.append(f"colour {alias} must equal {target} ({src_colors[target]})")

        # Fonts: each macro's font is the token's fontFamily.
        src_fonts = theme_fonts(theme)
        for macro, token in FONTS.items():
            want = src_fonts.get(macro)
            if want is None:
                errors.append(f"theme.h has no {macro} (&fonts::Name) definition")
                continue
            got = re.search(r"^    fontFamily: (\S+)", yaml_block(typography, token, 2), re.M)
            if not got or got.group(1) != want:
                errors.append(f"font {token}: theme.h {want}, DESIGN.md {got.group(1) if got else None}")
        for macro in src_fonts:
            if macro not in FONTS:
                errors.append(f"theme.h font {macro} has no typography token in DESIGN.md")

        consts = layout_consts(layout, errors)

        def need(name):
            if name not in consts:
                errors.append(f"layout.h has no constant {name}")
            return consts.get(name)

        # Geometry in the front matter.
        for const, (comp, prop) in GEOMETRY.items():
            want = need(const)
            got = re.search(rf"^    {prop}: (\d+)px", yaml_block(components, comp, 2), re.M)
            got = int(got.group(1)) if got else None
            if want is not None and got != want:
                errors.append(f"{const}: layout.h {want}, DESIGN.md {comp}.{prop} {got}")

        # Values the prose quotes.
        for const, pattern, what in (
            ("kBodyY", r"y = (\d+) \(`kBodyY`\)", "body start (kBodyY)"),
            ("kBodyH", r"for (\d+) px \(`kBodyH`\)", "body height (kBodyH)"),
        ):
            want = need(const)
            got = re.search(pattern, text)
            if want is not None and (not got or int(got.group(1)) != want):
                errors.append(f"Layout section: {what} is {got.group(1) if got else 'missing'}, layout.h {want}")
        for w, h, label in (("kMonoW", "kMonoH", "mono"), ("kSmallW", "kSmallH", "small")):
            cw, ch = need(w), need(h)
            if cw is not None and ch is not None and not re.search(rf"\*\*{label}\*\*[^\n]*\b{cw}x{ch}\b", text):
                errors.append(f"Typography section does not quote the {label} cell as {cw}x{ch}")
    except SyncError as e:
        errors.append(str(e))

    if errors:
        print("DESIGN.md is out of sync with src/ui/theme.h and layout.h:", file=sys.stderr)
        for e in errors:
            print("  -", e, file=sys.stderr)
        return 1
    print(f"DESIGN.md matches theme.h and layout.h ({len(src_colors)} colours, {len(FONTS)} fonts, geometry, glyph cells)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
