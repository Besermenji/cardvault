#!/usr/bin/env python3
"""Take the README and M5Burner screenshots from a Cardputer running the e2e build.

  pio run -e cardputer-e2e -t upload
  tools/screenshots.py /dev/cu.usbmodem1101 --yes [out_dir]      # default: docs/screenshots
  tools/screenshots.py /dev/cu.usbmodem1101 --yes --sd           # plus the SD update screens

This ERASES the e2e test vault (the "cvtvault" partition, never a real vault), creates a new one with
made-up entries (public test vectors only), drives the real UI to each main screen and saves a PNG of
each, starting with the boot splash (also the M5Burner cover). Secrets stay masked: nothing is
revealed. The clock is set to a fixed time before every capture so re-runs differ only in the battery
icon and the generated password on the generator screen.

Needs pyserial (PlatformIO's Python has it) and no SD card. With --sd (a FAT32 card, no other .cvu
files in /cardvault, OpenSSL 3, and the device running the build in .pio/build/cardputer-e2e) it also
takes the "Update firmware" screens for docs/UPDATING.md: it installs the running image labelled as the
next minor version, signed with the test key.
"""
import argparse
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


def capture(d, out, name, cover=False, still=False):
    """Save the screen as <name>.png (enlarged for the README) and, with
    `cover`, also as cover.png at 1:1 (M5Burner). `still`: the screen has no
    clock and takes no key (the boot splash), so nothing is woken or set."""
    if not still:
        d.wake()
        d.wait(lambda s: not s["toast"], "no toast on screen")
        d.cmd(f"settime {CLOCK}")
    w, h, rows = d.shot()
    for path, scale in [(out / f"{name}.png", SCALE)] + ([(out / "cover.png", 1)] if cover else []):
        screenshot.write_png(path, w, h, rows, scale)
        print(f"saved {path} ({w * scale}x{h * scale})", flush=True)


def open_menu_item(d, label, screen):
    run.go_home(d)
    d.key("tab")
    d.menu_pick(label)
    return d.screen(screen)


def leave_entry(d):
    """entry_view opened with run.open_entry -> home without the search."""
    d.key("esc")
    run.clear_search(d)


def next_minor_version():
    major, minor = run.source_version().split(".")[:2]
    return f"{major}.{int(minor) + 1}.0"


def update_screens(d, out):
    """The update list, the install question and the message after the restart."""
    version = next_minor_version()
    header, length = run.update_header(version=version)
    run.stage(d, run.UPDATE_FILE, header, length)
    try:
        _, items, _ = run.open_update_menu(d)
        if items != [f"v{version}"]:
            raise run.Fail(f"remove other .cvu files from /cardvault first: {items}")
        capture(d, out, "update-list")
        d.menu_pick(items[0])
        d.screen("confirm", "Update firmware", timeout=30)
        capture(d, out, "update-confirm")
        boot_id = run.ota(d)["boot_id"]
        d.cmd("key y")
        if not run.wait_install(d, boot_id):
            raise run.Fail("the install did not restart (is the device running the .pio e2e build?)")
        run.until(d, lambda s: s["screen"] == "message" and s["title"] == "Updated",
                  "the after-update message")
        capture(d, out, "update-done")
        d.key("enter")
        d.unlock(run.P1)
    finally:
        d.cmd(f"sdrm {run.UPDATE_FILE}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("out_dir", nargs="?", type=pathlib.Path, default=HERE.parent / "docs" / "screenshots")
    ap.add_argument("--yes", action="store_true", required=True,
                    help="confirm: the e2e test vault on the device is erased")
    ap.add_argument("--sd", action="store_true", help="a card is in: also take the SD update screens")
    args = ap.parse_intermixed_args()  # out_dir may follow --yes
    out = args.out_dir
    out.mkdir(parents=True, exist_ok=True)

    d = run.Device(args.port)
    d.skip_splash = False
    d.restart_cmd("factory")
    # The boot splash (the e2e build keeps it up for 8 s). It is the cover.
    d.wait(lambda s: s["screen"] == "splash" and run.info(s)["anim"] == "1",
           "boot splash, dial settled (slow USB reconnect? run again)")
    capture(d, out, "splash", cover=True, still=True)
    d.skip_splash = True
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

    run.open_entry(d, "Email")
    capture(d, out, "entry")
    leave_entry(d)

    run.open_entry(d, "Cold wallet")
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

    if args.sd:
        update_screens(d, out)
        run.go_home(d)
    print("done")


if __name__ == "__main__":
    try:
        main()
    except run.Fail as e:
        sys.exit(f"FAILED: {e}")
