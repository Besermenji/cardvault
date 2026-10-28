#!/usr/bin/env python3
"""Checks DESIGN.md: its front matter, and that it matches src/ui/theme.h and src/ui/layout.h.

- Front matter: parsed strictly (nested mappings of scalars, two-space indent; anything else is an
  error), checked against the design.md schema (known groups and properties, value formats), and
  every {group.token} reference must name an existing token.
- Sync: every colour of cv::ui::color, the font names, the screen geometry (kW, kH, kHeaderH,
  kFooterH, kBodyY, kBodyH, as layout.h computes them) and the glyph cells.
- Contrast: the WCAG ratios the Colors section quotes, on the colours as the screen shows them
  (RGB565). A reworded contrast sentence must be updated here too.
Component sizes and radii are kept by hand. Standard library only.
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
# The colours the Colors section calls state colours.
STATES = ("accent", "good", "bad", "warn", "info", "purple")
AA = 4.5

# design.md schema, as far as DESIGN.md uses it.
TOP_REQUIRED = {"version", "name", "colors", "typography", "components"}
TOP_OPTIONAL = {"description", "rounded", "spacing"}
TYPOGRAPHY_PROPS = {"fontFamily", "fontSize", "fontWeight", "lineHeight", "letterSpacing"}
# Component property -> token group a {reference} must point into.
COMPONENT_PROPS = {
    "backgroundColor": "colors", "textColor": "colors", "typography": "typography",
    "rounded": "rounded", "padding": "spacing", "size": "spacing", "height": "spacing", "width": "spacing",
}
HEX = re.compile(r"#[0-9A-Fa-f]{6}")
DIMENSION = re.compile(r"\d+px")
REF = re.compile(r"\{(\w+)\.([\w-]+)\}")


class SyncError(Exception):
    pass


def front_matter(text):
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    if not m:
        raise SyncError("DESIGN.md has no front matter")
    return m.group(1)


_LINE = re.compile(r"( *)([A-Za-z][\w-]*):(?: (.*))?")


def _scalar(value, lineno):
    if value.startswith('"'):
        m = re.fullmatch(r'"([^"\\]*)"', value)
        if not m:
            raise SyncError(f"DESIGN.md:{lineno}: unterminated or escaped string {value}")
        return m.group(1)
    if value[0] in "'[{&*!|>%@`" or ": " in value or " #" in value:
        raise SyncError(f"DESIGN.md:{lineno}: quote this value: {value}")
    return value


def parse_front_matter(src):
    """The YAML subset the front matter uses: nested mappings (two-space indent) of plain or
    double-quoted scalars. Anything else is an error, so a typo cannot slip past."""
    root = {}
    stack = [(0, root)]  # (indent of the entries, mapping)
    pending = None       # a "key:" whose mapping has no entries yet
    for lineno, line in enumerate(src.split("\n"), 2):  # line 1 is the opening ---
        if not line.strip():
            continue
        m = _LINE.fullmatch(line)
        if not m:
            raise SyncError(f"DESIGN.md:{lineno}: not a 'key: value' line: {line.strip()}")
        indent, key, value = len(m.group(1)), m.group(2), m.group(3)
        if pending is not None and indent != stack[-1][0]:
            raise SyncError(f"DESIGN.md:{lineno}: '{pending}:' needs entries indented by two more spaces")
        while indent < stack[-1][0]:
            stack.pop()
        if indent != stack[-1][0]:
            raise SyncError(f"DESIGN.md:{lineno}: bad indentation before '{key}'")
        mapping = stack[-1][1]
        if key in mapping:
            raise SyncError(f"DESIGN.md:{lineno}: duplicate key '{key}'")
        pending = None
        if value is None:
            mapping[key] = {}
            stack.append((indent + 2, mapping[key]))
            pending = key
        else:
            mapping[key] = _scalar(value.strip(), lineno)
    if pending is not None:
        raise SyncError(f"DESIGN.md front matter: '{pending}:' has no entries")
    return root


def check_schema(fm, errors):
    for key in TOP_REQUIRED - fm.keys():
        errors.append(f"front matter: missing '{key}'")
    for key in fm.keys() - TOP_REQUIRED - TOP_OPTIONAL:
        errors.append(f"front matter: unknown key '{key}'")

    def group(name):
        g = fm.get(name, {})
        if not isinstance(g, dict):
            errors.append(f"front matter: '{name}' must be a mapping")
            return {}
        return g

    def flat(name, pattern, what):
        for token, value in group(name).items():
            if not isinstance(value, str) or not pattern.fullmatch(value):
                errors.append(f"{name}.{token}: expected {what}, got {value!r}")

    flat("colors", HEX, "a #RRGGBB colour")
    flat("rounded", DIMENSION, "a <n>px dimension")
    flat("spacing", DIMENSION, "a <n>px dimension")

    for token, props in group("typography").items():
        if not isinstance(props, dict):
            errors.append(f"typography.{token}: must be a mapping")
            continue
        for prop in props.keys() - TYPOGRAPHY_PROPS:
            errors.append(f"typography.{token}: unknown property '{prop}'")
        for prop in ("fontFamily", "fontSize"):
            if prop not in props:
                errors.append(f"typography.{token}: missing {prop}")
        for prop, pattern in (("fontSize", DIMENSION), ("letterSpacing", DIMENSION), ("fontWeight", re.compile(r"\d+"))):
            if prop in props and not (isinstance(props[prop], str) and pattern.fullmatch(props[prop])):
                errors.append(f"typography.{token}.{prop}: bad value {props[prop]!r}")

    for comp, props in group("components").items():
        if not isinstance(props, dict):
            errors.append(f"components.{comp}: must be a mapping")
            continue
        for prop, value in props.items():
            where = f"components.{comp}.{prop}"
            target = COMPONENT_PROPS.get(prop)
            if target is None:
                errors.append(f"{where}: unknown property")
            elif not isinstance(value, str):
                errors.append(f"{where}: must be a value, not a mapping")
            elif value.startswith("{"):
                ref = REF.fullmatch(value)
                if not ref:
                    errors.append(f"{where}: malformed reference {value}")
                elif ref.group(1) != target:
                    errors.append(f"{where}: {value} must point into '{target}'")
                elif ref.group(2) not in group(target):
                    errors.append(f"{where}: {value} names no token")
            elif target == "colors" and not HEX.fullmatch(value):
                errors.append(f"{where}: expected a #RRGGBB colour or {{colors.*}}, got {value}")
            elif target == "typography":
                errors.append(f"{where}: expected a {{typography.*}} reference, got {value}")
            elif target not in ("colors", "typography") and not DIMENSION.fullmatch(value):
                errors.append(f"{where}: expected a <n>px dimension or {{{target}.*}}, got {value}")


def theme_colors(theme, errors):
    """theme.h colours as (r, g, b) exactly as written in rgb(...)."""
    # The colour namespace holds only constants, so it ends at the first closing brace.
    ns = re.search(r"namespace\s+color\s*\{([^{}]*)\}", theme)
    if not ns:
        raise SyncError("theme.h: namespace color { ... } not found")
    out = {}
    for name, value in re.findall(r"constexpr\s+uint16_t\s+(\w+)\s*=\s*([^;]+);", ns.group(1)):
        m = re.fullmatch(r"rgb\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)", value.strip())
        try:
            rgb = tuple(int(x, 0) for x in m.groups()) if m else None
        except ValueError:
            rgb = None
        if not rgb:
            errors.append(f"theme.h colour {name} = {value.strip()}: write it as rgb(r, g, b) so it can be checked")
            continue
        out[name] = rgb
    if not out:
        raise SyncError("theme.h: no colours found in namespace color")
    return out


def as_shown(rgb):
    """theme.h's rgb() keeps 5/6/5 bits; the panel widens each channel back by bit replication."""
    r, g, b = rgb[0] >> 3, rgb[1] >> 2, rgb[2] >> 3
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def contrast(fg, bg):
    def lum(rgb):
        def ch(v):
            v /= 255
            return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
        r, g, b = map(ch, rgb)
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    hi, lo = sorted((lum(fg), lum(bg)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def check_contrast(text, rgb, errors):
    prose = " ".join(text.split())
    shown = {k: as_shown(v) for k, v in rgb.items()}

    def ratio(fg, bg, raw=False):
        src = rgb if raw else shown
        return contrast(src[fg], src[bg])

    def claim(pattern, what):
        m = re.search(pattern, prose)
        if not m:
            errors.append(f"Colors section: the sentence on {what} is missing or reworded "
                          "(keep it in step with check_contrast in tools/check_design_sync.py)")
            return None
        return [float(x) for x in m.groups()], m.groups()

    def same(actual, quoted, label):
        decimals = len(quoted.partition(".")[2])
        if round(actual, decimals) != float(quoted):
            errors.append(f"contrast {label}: DESIGN.md says {quoted}:1, it is {actual:.{decimals}f}:1")

    c = claim(r"`text` on `bg`, `panel` and `sel` is at least ([\d.]+):1", "text contrast")
    if c:
        worst = min(ratio("text", b) for b in ("bg", "panel", "sel"))
        if worst < c[0][0]:
            errors.append(f"contrast text: DESIGN.md says at least {c[1][0]}:1, the lowest is {worst:.2f}:1")
    c = claim(r"every state colour on `bg` or `panel` is at least ([\d.]+):1", "state colours")
    if c:
        worst, pair = min((ratio(s, b), f"{s} on {b}") for s in STATES for b in ("bg", "panel"))
        if worst < c[0][0]:
            errors.append(f"contrast state colours: DESIGN.md says at least {c[1][0]}:1, {pair} is {worst:.2f}:1")
    c = claim(r"`dim` is ([\d.]+):1 on `bg` and ([\d.]+):1 on `panel`, so hints and labels pass AA", "dim")
    if c:
        for bg, quoted in zip(("bg", "panel"), c[1]):
            same(ratio("dim", bg), quoted, f"dim on {bg}")
            if ratio("dim", bg) < AA:
                errors.append(f"contrast dim on {bg}: below AA ({AA}:1) but DESIGN.md says it passes")
    c = claim(r"`dim` on `panel` would be ([\d.]+):1; what the screen shows is ([\d.]+):1", "dim before RGB565")
    if c:
        same(ratio("dim", "panel", raw=True), c[1][0], "dim on panel (hex)")
        same(ratio("dim", "panel"), c[1][1], "dim on panel (screen)")
    c = claim(r"\*\*Known limit:\*\* `dim` on `sel` is ([\d.]+):1 and `bad` on `sel` is ([\d.]+):1", "the known limit")
    if c:
        for fg, quoted in zip(("dim", "bad"), c[1]):
            same(ratio(fg, "sel"), quoted, f"{fg} on sel")
            if ratio(fg, "sel") >= AA:
                errors.append(f"contrast {fg} on sel now passes AA: drop it from the known limit")


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
    src_colors = {}
    try:
        fm = parse_front_matter(front_matter(text))
        check_schema(fm, errors)
        if errors:
            raise SyncError("sync and contrast checks skipped until the front matter is valid")
        components, typography = fm["components"], fm["typography"]

        # Colours: every theme.h colour, nothing extra, aliases equal their target.
        doc_colors = {k: v.upper() for k, v in fm["colors"].items()}
        src_rgb = theme_colors(theme, errors)
        src_colors = {k: "#%02X%02X%02X" % v for k, v in src_rgb.items()}
        for name, hexv in src_colors.items():
            if doc_colors.get(name) != hexv:
                errors.append(f"colour {name}: theme.h {hexv}, DESIGN.md {doc_colors.get(name)}")
        for name in doc_colors:
            if name not in src_colors and name not in ALIASES:
                errors.append(f"colour {name} is in DESIGN.md but not in theme.h")
        for alias, target in ALIASES.items():
            if target in src_colors and doc_colors.get(alias) != src_colors[target]:
                errors.append(f"colour {alias} must equal {target} ({src_colors[target]})")
        missing = [c for c in ("text", "dim", "bg", "panel", "sel", *STATES) if c not in src_rgb]
        if missing:
            errors.append(f"theme.h lacks {', '.join(missing)}; cannot check contrast")
        else:
            check_contrast(text, src_rgb, errors)

        # Fonts: each macro's font is the token's fontFamily.
        src_fonts = dict(re.findall(r"#define\s+(CV_FONT_\w+)\s+\(&fonts::(\w+)\)", theme))
        for macro, token in FONTS.items():
            want = src_fonts.get(macro)
            if want is None:
                errors.append(f"theme.h has no {macro} (&fonts::Name) definition")
                continue
            got = typography.get(token, {}).get("fontFamily")
            if got != want:
                errors.append(f"font {token}: theme.h {want}, DESIGN.md {got}")
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
            got = components.get(comp, {}).get(prop, "")
            got = int(got[:-2]) if DIMENSION.fullmatch(got) else None
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
        print("DESIGN.md has problems (front matter, or out of sync with src/ui/theme.h and layout.h):", file=sys.stderr)
        for e in errors:
            print("  -", e, file=sys.stderr)
        return 1
    print(f"DESIGN.md is valid and matches theme.h and layout.h "
          f"({len(src_colors)} colours, {len(FONTS)} fonts, geometry, glyph cells, contrast)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
