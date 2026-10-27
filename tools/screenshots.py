#!/usr/bin/env python3
"""Take the README and M5Burner screenshots from a Cardputer running the e2e build.

  pio run -e cardputer-e2e -t upload
  python3 tools/screenshots.py /dev/cu.usbmodem1101 --yes [out_dir]      # default: docs/screenshots

This ERASES the e2e test vault (the "cvtvault" partition, never a real vault), creates a new one with
made-up entries (public test vectors only), drives the real UI to each main screen and saves a PNG of
each. Secrets stay masked: nothing is revealed. The clock is set to a fixed time before every capture so
re-runs differ only in the battery icon and the generated password on the generator screen.

Needs pyserial (PlatformIO's Python has it) and no SD card.
"""
import calendar
import pathlib
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "e2e"))

import run  # noqa: E402  (tools/e2e/run.py: the device driver and the scenarios)
import screenshot  # noqa: E402  (tools/screenshot.py: PNG writer)

# 2026-11-02 09:41:10 UTC, moved so that it is 10 s into a TOTP period: the code and the
# countdown bar cannot roll over in the few seconds between setting the clock and the capture.
CLOCK = calendar.timegm((2026, 11, 2, 9, 41, 0))
CLOCK += (10 - CLOCK % 30) % 30
SCALE = 2  # README images; the cover is 1:1


def capture(d, out, name, scale=SCALE):
    d.wait(lambda s: not s["toast"] and not s["dimmed"], "no toast on screen")
    d.cmd(f"settime {CLOCK}")
    w, h, rows = d.shot()
    path = out / f"{name}.png"
    screenshot.write_png(path, w, h, rows, scale)
    print(f"saved {path} ({w * scale}x{h * scale})", flush=True)


def open_menu_item(d, label, screen):
    run.go_home(d)
    d.key("tab")
    d.menu_pick(label)
    return d.screen(screen)


def open_entry(d, title):
    """Home -> search for `title` -> its entry_view."""
    run.go_home(d)
    d.type(title)
    d.wait(lambda s: run.info(s)["rows"] == "1", f"search finds {title}")
    d.key("enter")
    d.screen("entry_view")


def leave_entry(d):
    d.key("esc")
    d.screen("home")
    d.key("esc")  # clear the search
    d.screen("home")


def main():
    args = [a for a in sys.argv[1:] if a != "--yes"]
    if "--yes" not in sys.argv[1:] or not 1 <= len(args) <= 2:
        sys.exit(__doc__)
    port = args[0]
    out = pathlib.Path(args[1]) if len(args) > 1 else HERE.parent / "docs" / "screenshots"
    out.mkdir(parents=True, exist_ok=True)

    d = run.Device(port)
    d.restart_cmd("factory")
    d.screen("setup")

    # Made-up content: t02 creates the vault, t03/t04/t07/t08 add a note, a seed phrase
    # ("abandon x11 about", the BIP39 test vector), a TOTP entry (a well-known test secret)
    # and a generated password.
    run.t02_setup_validation_and_create(d)
    run.t03_add_note_multiline(d)
    run.t04_add_seed_with_autocomplete(d)
    run.t07_totp_validation_and_code(d)
    run.t08_password_generator_to_entry(d)

    run.go_home(d)
    d.lock()
    capture(d, out, "unlock")
    d.unlock(run.P1)

    run.go_home(d)
    capture(d, out, "home")
    capture(d, out, "cover", scale=1)

    open_entry(d, "Email")
    capture(d, out, "entry")
    leave_entry(d)

    open_entry(d, "Cold wallet")
    d.key("enter")  # Words
    d.screen("seed_view")
    capture(d, out, "seed")
    d.key("esc")
    d.screen("entry_view")
    leave_entry(d)

    open_menu_item(d, "2FA codes", "totp_list")
    capture(d, out, "totp")
    d.key("esc")

    open_menu_item(d, "Password generator", "pwgen")
    capture(d, out, "generator")
    d.key("esc")

    open_menu_item(d, "Settings", "settings")
    capture(d, out, "settings")
    d.key("esc")
    run.go_home(d)
    print("done")


if __name__ == "__main__":
    try:
        main()
    except run.Fail as e:
        sys.exit(f"FAILED: {e}")
