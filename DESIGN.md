---
version: alpha
name: CardVault
description: On-device UI of CardVault, an encrypted secrets vault for the M5Stack Cardputer ADV (240x135 LCD, keyboard only).
colors:
  primary: "#F7931A"
  bg: "#0B0E14"
  panel: "#181D28"
  sel: "#26304A"
  line: "#303848"
  text: "#E6E9EF"
  dim: "#7A8499"
  accent: "#F7931A"
  good: "#3FB950"
  bad: "#F85149"
  warn: "#D29922"
  info: "#58A6FF"
  purple: "#BC8CFF"
typography:
  mono:
    fontFamily: AsciiFont8x16
    fontSize: 16px
    fontWeight: 400
  small:
    fontFamily: Font0
    fontSize: 8px
    fontWeight: 400
  title:
    fontFamily: FreeSansBold9pt7b
    fontSize: 12px
    fontWeight: 700
  big:
    fontFamily: Orbitron_Light_32
    fontSize: 32px
    fontWeight: 300
rounded:
  sm: 3px
  md: 4px
  lg: 6px
spacing:
  xs: 2px
  sm: 4px
  md: 8px
components:
  screen:
    backgroundColor: "{colors.bg}"
    textColor: "{colors.text}"
    typography: "{typography.mono}"
    width: 240px
    height: 135px
  header:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.text}"
    typography: "{typography.title}"
    height: 18px
  header-clock:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.dim}"
    typography: "{typography.small}"
  footer:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.dim}"
    typography: "{typography.small}"
    height: 11px
    padding: 4px
  list-row:
    backgroundColor: "{colors.bg}"
    textColor: "{colors.text}"
    typography: "{typography.mono}"
  list-row-selected:
    backgroundColor: "{colors.sel}"
    textColor: "{colors.text}"
    rounded: "{rounded.sm}"
  type-badge:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.sm}"
    size: 14px
  dialog:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.text}"
    typography: "{typography.small}"
    rounded: "{rounded.lg}"
  toast-good:
    backgroundColor: "{colors.good}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.md}"
    height: 16px
  toast-warn:
    backgroundColor: "{colors.warn}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.md}"
    height: 16px
  toast-bad:
    backgroundColor: "{colors.bad}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.md}"
    height: 16px
  badge-password:
    backgroundColor: "{colors.info}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.sm}"
    size: 14px
  badge-totp:
    backgroundColor: "{colors.good}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.sm}"
    size: 14px
  badge-note:
    backgroundColor: "{colors.purple}"
    textColor: "{colors.bg}"
    typography: "{typography.small}"
    rounded: "{rounded.sm}"
    size: 14px
  frame:
    backgroundColor: "{colors.line}"
    textColor: "{colors.text}"
---

# CardVault on-device UI

The source of truth is [`src/ui/theme.h`](src/ui/theme.h) (colours, fonts) and
[`src/ui/layout.h`](src/ui/layout.h) (geometry, glyph sizes). Drawing helpers live in
[`src/ui/gfx.cpp`](src/ui/gfx.cpp). This file describes them for people and agents who add screens;
change it in the same commit as those files. `tools/check_design_sync.py` (run in CI) fails when the front
matter is malformed or a `{token}` reference is broken, when a contrast figure below is wrong, or when the colours,
the font names, the screen geometry (`kW`, `kH`, `kHeaderH`, `kFooterH`, `kBodyY`, `kBodyH`) or the glyph cells
here differ from the headers. Component sizes, radii and row heights are kept by hand.

## Overview

CardVault runs on the Cardputer ADV: a 240x135 colour LCD and a 56-key keyboard, no touch screen, no
mouse. Everything is a full-screen view drawn on an off-screen canvas, operated from the keyboard.
The look is a dark, quiet palette with one warm accent (Bitcoin orange). The screen is small and read
at arm's length, so text is short, contrast is high and every screen says which keys it takes.

Secrets are the thing the UI protects: they are masked until the user asks, hide again by themselves
and never appear in a toast, a title or a hint.

## Colors

The device draws RGB565, so each channel is quantised (5/6/5 bits) and the colours on the panel differ
slightly from the hex values in the front matter, which are the ones passed to `rgb()` in `theme.h`. Use the names
in `cv::ui::color`, never a raw literal.

- **bg**: the screen background.
- **panel**: header, footer, dialogs, the search bar.
- **sel**: the selected list row.
- **line**: thin rules, scrollbar track, progress track.
- **text**: primary text and values.
- **dim**: hints, labels, masks, the clock, secondary text.
- **accent** / **primary**: the header underline, selection bar, focus, secret entries
  (type `S`). `primary` is the same colour under the name the design spec expects.
- **good**: success, a full battery, 2FA entries (type `2`).
- **bad**: errors, a low battery (< 20 %), wrong passphrase.
- **warn**: destructive or limiting situations, battery < 50 %, a vault that is nearly full.
- **info**: password entries (type `P`).
- **purple**: note entries (type `N`).

Contrast on the RGB565 values, computed from the WCAG formula: `text` on `bg`, `panel` and `sel` is at least
11:1 and every state colour on `bg` or `panel` is at least 5.2:1. `dim` is 5.4:1 on `bg` and 4.6:1 on `panel`,
so hints and labels pass AA there. On the hex values above, before the RGB565 rounding, `dim` on `panel` would
be 4.49:1; what the screen shows is 4.63:1. **Known limit:** `dim` on `sel` is 3.6:1 and `bad` on `sel` is
4.1:1, so do not put `dim` or `bad` text on a selected row; use `text`. `tools/check_design_sync.py` checks
these figures.

## Typography

All fonts are bitmap or GFX fonts that M5GFX ships; they do not scale, so the sizes are glyph cells.

- **mono** (`CV_FONT_MONO`, `AsciiFont8x16`, 8x16): values, list rows and everything the user types.
  30 columns across the screen.
- **small** (`CV_FONT_SMALL`, `Font0`, 6x8): footer hints, labels, the clock, dialog bodies and toasts.
  40 columns across the screen.
- **title** (`CV_FONT_TITLE`, `FreeSansBold9pt7b`): the header title and dialog titles.
- **big** (`CV_FONT_BIG`, `Orbitron_Light_32`): TOTP codes and the countdown, nothing else.

Keep the glyph sizes in `layout.h` (`kMonoW/H`, `kSmallW/H`) equal to the fonts above.

## Layout

The grid is 240x135 pixels (`kW`, `kH`).

- Header: the top 18 px (`kHeaderH`), title on the left, clock and battery on the right, a 2 px underline
  in the screen's accent colour.
- Footer: the bottom 11 px (`kFooterH`), key hints in `small` on `panel`, starting 4 px from the left.
- Body: from y = 20 (`kBodyY`) for 103 px (`kBodyH`), 8 px side margins for content. The scrollbar takes
  the right 4 px.
- Row heights depend on what a row shows: 15 px in Settings, 17 px in menus, 18 px in the vault list,
  21 px in the editor, 24 px in the entry view, 26 px for 2FA codes. A list fills the body from its first row
  down: visible rows = `(kBodyY + kBodyH - first_row_y) / row_h`, so a list under a search bar or other header
  line gets fewer rows than `kBodyH / row_h`.
- Columns: 30 `mono` or 40 `small` characters; the generator box (8 px margins) fits 28 `mono` or 37 `small`.

Anything wider than the screen wraps (`ui/textwrap.h`) or scrolls; it is never clipped silently.

## Elevation & Depth

There is no shadow or blur. Depth is three flat steps: `bg` (screen), `panel` (bars and dialogs) and
`sel` (the selected row). A dialog sits on a 2 px offset `bg` rectangle and a 1 px border in its accent
colour.

## Shapes

Corners are small: 3 px on rows and badges, 4 px on toasts, 6 px on dialogs. Rules are 1 to 2 px. The
progress bar is a pill (radius = half its height). Bars (header, footer) are square.

## Components

- **Header** (`header(title, accent)`): `panel` bar with the title in `title`; the underline is the
  screen's accent (`accent` by default, `warn` when the vault is nearly full). The right side shows
  the time (only when the clock is set) in `dim` and a battery icon coloured by charge.
- **Footer** (`footer(hints)`): `dim` text on `panel`. Write hints as `KEY action`, separated by two spaces,
  at most about 38 characters. `` ` `` is back or cancel, ENTER selects, TAB opens more, DEL goes back one
  step. Name keys the way the keyboard labels them.
- **List row** (`list_row`): unselected rows are plain `bg`; the selected row is a `sel` rounded rectangle
  (radius 3) with a 2 px `accent` bar on its left edge and `text` content. Long lists show the
  **scrollbar** (2 px wide at x = 236, `line` track, `dim` thumb).
- **Type badge** (`type_badge`): a 14x14 rounded square with one `small` letter in `bg`:
  `S` accent (seed), `P` info (password), `2` good (2FA), `N` purple (note).
- **Dialog** (`dialog`): a centred rounded box on `panel` with the title in `title` and a wrapped `small`
  message in `text`; the border and title use the accent that fits the situation (`warn` to confirm,
  `bad` for errors).
- **Toast** (`App::toast`): a 16 px box with 4 px corners, centred 4 px above the footer, text in `bg` on
  the state colour: `good` for saved, `warn` for deleted, refused or limits, `bad` for errors. One short
  line, no secret in it.
- **Masked secret** (`mask`): `dim` dots of 2 px radius, one per character, in place of the value. Holding
  SPACE peeks, R reveals everything, ENTER on a row reveals just that row; a revealed secret hides again
  after 20 s or when the screen dims.
- **Progress bar** (`progress`): `line` pill with a coloured fill (TOTP countdown, calibration).
- **Splash** (`draw_splash`, `make_splash`): the first screen after power-on, with no header, footer, clock
  or battery. A vault-door dial (radius 46, centred 56 px from the left) sits on the left: a `panel` ring
  with 24 tick marks that turn 90 degrees with an ease-out over 800 ms, four spokes, a hub and four bolts.
  While it turns the bolts and hub are `dim`; once it has settled they are `accent`. On the right are the
  name in `title`, a 40x2 px `accent` rule, the tagline in `small` on `dim` and the version at the bottom right. The splash
  ends after 1.5 s or on any key; that key is not passed on. It only appears at boot: locking and
  unlocking go straight to the next screen.

## Do's and Don'ts

- Do take every colour from `cv::ui::color`. A new colour goes into `theme.h` first, then here, with a
  stated purpose.
- Do list the keys a screen takes in its footer, and keep `` ` `` as back or cancel everywhere.
- Do mask secrets until the user asks, and ask before anything destructive (delete, erase, discarding
  edits).
- Do keep text readable at arm's length: `dim` only on `bg` or `panel`, `text` on `sel`.
- Don't put a secret, a passphrase or a fragment of one in a toast, title, hint, log line or dialog.
- Don't use colour alone to carry meaning; pair it with a word or a letter (type badges have a letter).
- Don't add a screen that needs a mouse, touch, Wi-Fi or Bluetooth. None of them exist in CardVault.
- Don't hard-code the geometry; use `kW`, `kH`, `kHeaderH`, `kFooterH`, `kBodyY`, `kBodyH`.
