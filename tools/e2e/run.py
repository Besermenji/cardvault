#!/usr/bin/env python3
"""End-to-end tests: drive CardVault's real UI on a Cardputer over USB.

  pio run -e cardputer-e2e -t upload
  python3 tools/e2e/run.py /dev/cu.usbmodem1101 [-k name_filter] [--sd]

Without --sd the device must have NO SD card: the SD scenarios then check
the "No SD card" paths. With --sd a FAT32 card must be inserted: backup,
restore (also from the setup screen, #43) and Compact run against it, and
leave backup-*.cvb files in /cardvault on the card.

The e2e firmware keeps its vault in its own "cvtvault" partition (0x410000) and
uses separate NVS namespaces, so a real vault on the device is never touched. Requires
pyserial (PlatformIO's Python has it). t25 dumps flash with PlatformIO's esptool, run by
this Python or PlatformIO's own (found via `pio system info`); it skips without one.
"""
import argparse
import hashlib
import hmac
import json
import re
import os
import struct
import subprocess
import sys
import tempfile
import time
import traceback

import serial

P1 = "correct horse 42"
P2 = "battery staple 77"
P3 = "backup phrase 99"
SD_CARD = False  # set by --sd
TOTP_SECRET = "JBSWY3DPEHPK3PXP"

# The Cardputer keyboard matrix (column x 0-13, row y 0-3), copied from
# M5Cardputer 1.1.1 (utility/Keyboard/Keyboard.h, _key_value_map; the version is
# pinned in platformio.ini). Printable keys: (unshifted, shifted); the others
# are named.
_MATRIX = [
    list(zip("`1234567890-=", "~!@#$%^&*()_+")) + ["bs"],
    ["tab"] + list(zip("qwertyuiop[]\\", "QWERTYUIOP{}|")),
    ["fn", "shift"] + list(zip("asdfghjkl;'", 'ASDFGHJKL:"')) + ["enter"],
    ["ctrl", "opt", "alt"] + list(zip("zxcvbnm,./", "ZXCVBNM<>?")) + ["space"],
]
KEYPOS = {}      # key name -> (x, y); a printable key is named by its unshifted character
PRINTABLE = []   # (name, unshifted, shifted) for every key that types a character
for _y, _row in enumerate(_MATRIX):
    assert len(_row) == 14, _row
    for _x, _k in enumerate(_row):
        if isinstance(_k, tuple):
            KEYPOS[_k[0]] = (_x, _y)
            PRINTABLE.append((_k[0], _k[0], _k[1]))
        else:
            KEYPOS[_k] = (_x, _y)
PRINTABLE.append(("space", " ", " "))


class Fail(Exception):
    pass


class Skip(Exception):
    pass


# "OK <id> <payload>" / "ERR <id> <message>"; id is "-" when the device could
# not read one.
REPLY = re.compile(r"^(OK|ERR) (\d+|-)(?: (.*))?$")


class Device:
    def __init__(self, port):
        self.port = port
        self.ser = None
        self.seq = 0  # last request id
        self.rx = b""  # bytes read but not yet split into lines
        self.stale = 0  # replies that arrived for an earlier request
        self.unreadable = []  # requests the device could not parse ("ERR -")
        self.noise = []  # serial lines that are not replies (boot output, stray prints)
        self.fragments = []  # tails of reply lines cut by a reconnect (harness traffic)
        self.fresh = False  # no complete line read since the port was opened
        self.typed = set()  # every string of 4+ characters sent with `type` (see secrets())
        self.connect()

    # -- transport ---------------------------------------------------------------

    def connect(self, timeout=25):
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.ser = serial.Serial(self.port, 115200, timeout=0.2)
                time.sleep(0.3)
                # Keep what the device printed before we ask anything (boot
                # output): t26 checks it. Replies still queued are stale.
                self.rx = b""
                self.fresh = True
                self._drain()
                if self.cmd("ping", timeout=2) == "pong":
                    return
            except (serial.SerialException, OSError, Fail):
                pass
            # Lines read while waiting for the ping are already kept; a port
            # that keeps talking must not cost 2 s per retry.
            self.close(drain=0.2)
            time.sleep(0.5)
        raise Fail(f"device on {self.port} not responding")

    def close(self, drain=2.0):
        """Read what is left on the link for up to `drain` seconds (for t26),
        then close it."""
        try:
            if self.ser:
                self._drain(drain)
        except (serial.SerialException, OSError):
            pass  # the link is already gone (reboot)
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass

    def _drain(self, limit=2.0):
        """Read until nothing more arrives (at most `limit` seconds)."""
        end = time.time() + limit
        while self.ser.in_waiting and time.time() < end:
            self._pump()

    def _pump(self):
        """Read what has arrived and return the complete reply lines. Other
        lines are device output (`noise`), except the tail of a reply line whose
        start was lost, which is harness traffic (`fragments`). Only the first
        line after opening the port can be one (a reconnect cuts at most the
        line in flight); it counts as a fragment only if it ends like a data
        reply (JSON: } or ]). Every later line is checked by t26."""
        self.rx += self.ser.read(max(256, self.ser.in_waiting))
        replies = []
        while b"\n" in self.rx:
            raw, self.rx = self.rx.split(b"\n", 1)
            text = raw.decode(errors="replace").strip()
            first, self.fresh = self.fresh, False
            m = REPLY.match(text)
            if m:
                replies.append(m)
            elif first and text.endswith(("}", "]")):
                self.fragments.append(text)
            elif text:
                self.noise.append(text)  # boot output, CVTEST READY
        return replies

    def cmd(self, line, timeout=10):
        """Send `line` with a fresh request id and return the payload of the
        reply that carries the same id. Replies to earlier requests (late
        answers after a timeout) are counted and skipped. A dropped link is
        not retried: on native USB it means the device reset, and resending
        would hide that."""
        self.seq += 1
        rid = str(self.seq)
        if line.startswith("type ") and len(line) >= 9:
            self.typed.add(line[5:])
        self.ser.write(f"{rid} {line}\n".encode())
        end = time.time() + timeout
        while time.time() < end:
            for m in self._pump():
                kind, got, payload = m.group(1), m.group(2), m.group(3) or ""
                if got == "-":
                    # Some line had no readable id: maybe ours, maybe an old
                    # fragment. Not proof either way; keep waiting for ours.
                    self.unreadable.append(payload)
                    continue
                if got != rid:
                    self.stale += 1
                    continue
                if kind == "ERR":
                    raise Fail(f"{line!r}: {payload}")
                return payload
        note = f" (device could not parse: {self.unreadable[-3:]})" if self.unreadable else ""
        raise Fail(f"no reply to {line!r}{note}")

    def restart_cmd(self, line):
        """Send a command that reboots the device, then reconnect. The device
        answers before it restarts; an ERR (a failed factory reset) fails."""
        try:
            self.cmd(line, timeout=30)
        except (serial.SerialException, OSError):
            pass  # the link went away with the reboot
        time.sleep(1.5)
        self.close()
        self.connect()

    def hard_reset(self):
        """Pulse EN via RTS (like pulling power) and reconnect."""
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        time.sleep(1.5)
        self.close()
        self.connect()

    # -- helpers -----------------------------------------------------------------

    def state(self):
        return json.loads(self.cmd("state"))

    def dump(self):
        """The unlocked session's entries (RAM only: says nothing about flash)."""
        return json.loads(self.cmd("dump"))

    def storage(self):
        """What is really stored: vault files, the raw partition, the NVS counter."""
        return json.loads(self.cmd("storage", timeout=30))

    def shot(self):
        """The screen as the device draws it now: (width, height, rows), one
        4-hex-digit RGB565 value per pixel."""
        w, h, hexrows = self.cmd("shot", timeout=30).split(" ")
        w, h = int(w), int(h)
        if len(hexrows) != w * h * 4:
            raise Fail(f"shot: expected {w * h * 4} hex digits, got {len(hexrows)}")
        return w, h, [hexrows[y * w * 4:(y + 1) * w * 4] for y in range(h)]

    def key(self, name, times=1):
        # The device handles each request in order and drains its key queue on
        # every loop, so a reply-per-key keeps keys from piling up behind one
        # that changes the screen (which discards the queue).
        for _ in range(times):
            self.cmd(f"key {name}")
        time.sleep(0.05)

    def type(self, text):
        self.cmd(f"type {text}")
        time.sleep(0.05)

    def keys(self, *names):
        """Hold exactly these keys (matrix positions through the library's own
        key list, key state and keymap); no names releases everything."""
        self.cmd("keys " + " ".join("%d,%d" % KEYPOS[n] for n in names))

    def keyseq(self, ms, *steps):
        """Hold each step's keys (a tuple of names; () releases all) in turn,
        `ms` apart. The device times it, so the holds do not depend on how
        fast this host answers (a command round trip is about 200 ms)."""
        sets = "|".join(" ".join("%d,%d" % KEYPOS[n] for n in step) for step in steps)
        self.cmd(f"keyseq {ms} {sets}", timeout=10 + ms * len(steps) / 1000)

    def tap(self, name, *mods, ms=40):
        """Press `name` together with the modifier keys, hold it for `ms`
        (short of the auto-repeat delay), release."""
        if mods:
            self.keyseq(ms, (*mods, name), mods, ())
        else:
            self.keyseq(ms, (name,), ())

    def roll(self, *names, ms=40):
        """Press the keys one after the other, each before the last is let go,
        then release them in the same order."""
        up = [names[:i + 1] for i in range(len(names))]
        down = [names[i + 1:] for i in range(len(names))]
        self.keyseq(ms, *up, *down)

    def hold(self, ms, *names):
        """Hold the keys for `ms` on the device's clock, then release."""
        self.keyseq(ms, names, ())

    def wait(self, pred, what, timeout=20):
        end = time.time() + timeout
        st = None
        while time.time() < end:
            st = self.state()
            if pred(st):
                return st
            time.sleep(0.2)
        raise Fail(f"timeout waiting for {what}; last state: {st}")

    def screen(self, name, title=None, timeout=20):
        return self.wait(
            lambda s: s["screen"] == name and (title is None or s["title"] == title),
            f"screen {name}" + (f" '{title}'" if title else ""), timeout)

    def toast(self, contains, timeout=5):
        return self.wait(lambda s: contains in s["toast"], f"toast '{contains}'", timeout)

    def menu_pick(self, label):
        st = self.screen("menu")
        items = info(st)["items"].split("|")
        if label not in items:
            raise Fail(f"menu '{st['title']}' has no '{label}': {items}")
        # Arrow keys + ENTER: digit hotkeys only select destructive items.
        self.key("down", items.index(label))
        self.key("enter")

    def wake(self):
        """A dimmed screen swallows the first key: press one to wake it."""
        st = self.state()
        if st["dimmed"]:
            self.key("esc")
            st = self.wait(lambda s: not s["dimmed"], "screen awake")
        return st

    def unlock(self, pw):
        self.screen("unlock")
        self.wake()
        self.type(pw)
        self.key("enter")
        return self.screen("home", timeout=30)

    def lock(self):
        self.key("backtick")
        return self.screen("unlock")


def info(st):
    out = {}
    for part in re.split(r" (?=\w+=)", st["info"]):
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v
    return out


def check(cond, msg):
    if not cond:
        raise Fail(msg)


def assert_vault_stored(d):
    """A vault file exists and its ciphertext is in the raw partition."""
    st = d.storage()
    check(st["scanned"] and st["mounted"] and st["vault"] and st["magic_hits"] >= 1,
          f"expected a stored vault: {st}")
    return st


def assert_wiped(d):
    """Nothing of the vault is left: no files, no vault bytes anywhere in the
    partition (old copy-on-write copies included), counter reset to 0."""
    st = d.storage()
    check(st["scanned"], f"could not read the partition: {st}")
    check(not (st["vault"] or st["tmp"] or st["bak"]), f"vault files left after wipe: {st}")
    check(st["magic_hits"] == 0, f"vault bytes left in the partition after wipe: {st}")
    check(st["counter"] == "ok:0", f"attempt counter not reset after wipe: {st}")
    return st


def totp(secret_b32, t, period=30, digits=6):
    import base64
    key = base64.b32decode(secret_b32 + "=" * (-len(secret_b32) % 8))
    mac = hmac.new(key, struct.pack(">Q", t // period), hashlib.sha1).digest()
    off = mac[-1] & 0x0F
    code = (struct.unpack(">I", mac[off:off + 4])[0] & 0x7FFFFFFF) % (10 ** digits)
    return str(code).zfill(digits)


def go_home(d):
    for _ in range(6):
        st = d.state()
        if st["screen"] == "home":
            return st
        if st["screen"] == "confirm":
            d.key("y")
        else:
            d.key("esc")
        time.sleep(0.2)
    return d.screen("home")


def new_entry(d, type_label):
    go_home(d)
    d.key("tab")
    d.menu_pick("New secret")
    d.menu_pick(type_label)
    return d.screen("entry_edit")


def save_entry(d):
    d.key("tab")  # TAB only moves to the Save row
    d.key("enter")


# Settings rows (0-based)
ROW_BACKUP, ROW_RESTORE, ROW_COMPACT = 4, 5, 7


def open_setting(d, row):
    go_home(d)
    d.key("tab")
    d.menu_pick("Settings")
    d.screen("settings")
    d.key("down", row)
    d.key("enter")


def empty_backup_passphrase_refused(d):
    """Regression #32: ENTER and TAB on an empty Backup passphrase only warn.
    No key is derived, no attempt is counted, the prompt stays open."""
    d.screen("text_input", "Backup passphrase")
    fails = d.state()["fails"]
    for k in ("enter", "tab"):
        d.key(k)
        d.toast("At least 1")
        st = d.screen("text_input", "Backup passphrase")
        check(st["fails"] == fails, f"an empty backup passphrase must not count: {st}")
        d.wait(lambda s: s["toast"] == "", "warning gone", 8)


def backup_name_in(st):
    m = re.search(r"backup-\d+\.cvb", info(st).get("msg", ""))
    check(m is not None, f"no backup file name in: {st}")
    return m.group(0)


def need_card():
    if not SD_CARD:
        raise Skip("needs --sd and a FAT32 card in the device")


def find_one(d, title):
    """The single entry with this title (fails on none or several)."""
    found = [e for e in d.dump() if e["title"] == title]
    check(len(found) == 1, f"expected one entry titled {title!r}, found {len(found)}")
    return found[0]


def open_entry(d, title):
    """Home -> search for `title` -> its entry_view."""
    go_home(d)
    d.type(title)
    d.wait(lambda s: info(s)["rows"] == "1", f"search finds {title}")
    d.key("enter")
    return d.screen("entry_view")


def clear_search(d):
    """On home with a search query: Esc clears it (it does not lock)."""
    d.screen("home")
    d.key("esc")
    return d.wait(lambda s: s["screen"] == "home" and info(s)["query"] == "", "search cleared")


def delete_entry(d, title):
    open_entry(d, title)
    d.key("tab")
    d.menu_pick("Delete")
    d.screen("confirm", "Delete secret?")
    d.key("y")
    clear_search(d)


def set_title(d, title):
    st = d.screen("entry_edit")
    while info(st)["sel"] != "0":
        d.key("up")
        st = d.state()
    d.key("enter")
    d.screen("text_input", "Title")
    d.type(title)
    d.key("enter")
    d.screen("entry_edit")


# ---- scenarios -------------------------------------------------------------------

def t01_factory_boot_shows_setup(d):
    d.restart_cmd("factory")
    st = d.screen("setup")
    check(not st["open"] and st["fails"] == 0, f"unexpected state {st}")
    assert_wiped(d)


def t02_setup_validation_and_create(d):
    d.screen("setup")
    d.key("enter")
    d.screen("text_input", "New passphrase")
    d.type("short")
    d.key("enter")
    d.toast("At least 8")
    d.screen("text_input", "New passphrase")
    d.key("bs", 5)
    d.type(P1)
    d.key("enter")
    d.screen("text_input", "Confirm passphrase")
    d.type("something else")
    d.key("enter")
    d.toast("did not match")
    d.screen("text_input", "New passphrase")
    d.type(P1)
    d.key("enter")
    d.screen("text_input", "Confirm passphrase")
    d.type(P1)
    d.key("enter")
    st = d.screen("home", timeout=40)
    check(st["open"] and st["entries"] == 0 and st["depth"] == 1, f"bad home state {st}")


def t03_add_note_multiline(d):
    new_entry(d, "Note")
    set_title(d, "Shopping")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    d.type("milk")
    d.key("enter")  # newline in multi-line editor
    d.type("eggs")
    d.key("tab")
    d.screen("entry_edit")
    save_entry(d)
    d.screen("entry_view")
    notes = [e for e in d.dump() if e["title"] == "Shopping"]
    check(len(notes) == 1 and notes[0]["fields"]["1"] == "milk\neggs", f"note not saved: {notes}")


def t04_add_seed_with_autocomplete(d):
    new_entry(d, "Seed phrase")
    set_title(d, "Cold wallet")
    d.key("down")  # Words
    d.key("enter")
    d.menu_pick("Type 12 words")
    d.screen("mnemonic_input")
    for _ in range(11):
        d.type("aban")  # unique 4-letter prefix: auto-accepted
    st = d.state()
    check(info(st)["index"] == "11", f"expected word 12, got {st}")
    d.type("abou")
    d.toast("Checksum OK")
    d.screen("entry_edit")
    save_entry(d)
    d.screen("entry_view")
    seeds = [e for e in d.dump() if e["title"] == "Cold wallet"]
    expected = " ".join(["abandon"] * 11 + ["about"])
    check(seeds and seeds[0]["fields"]["2"] == expected, f"seed not saved: {seeds}")


def t05_seed_privacy_reveal(d):
    st = d.screen("entry_view")
    check(info(st)["reveal_all"] == "0", "secrets must start masked")
    d.cmd("hold space 1")
    d.wait(lambda s: info(s).get("held") == "1", "hold-to-reveal")
    d.cmd("hold space 0")
    d.wait(lambda s: info(s).get("held") == "0", "release hides again")
    d.key("enter")  # Words -> seed viewer
    st = d.screen("seed_view")
    check(info(st)["show"] == "0" and info(st)["words"] == "12", f"seed view {st}")
    d.cmd("hold space 1")
    d.wait(lambda s: info(s)["show"] == "1", "seed revealed while held")
    d.cmd("hold space 0")
    d.wait(lambda s: info(s)["show"] == "0", "seed hidden after release")
    d.key("r")
    d.wait(lambda s: info(s)["show"] == "1", "R toggles reveal")
    d.key("right")
    d.wait(lambda s: info(s)["page"] == "1", "next page")
    d.key("esc")
    d.screen("entry_view")


def t06_bad_checksum_requires_confirmation(d):
    new_entry(d, "Seed phrase")
    d.key("down")
    d.key("enter")
    d.menu_pick("Type 12 words")
    for _ in range(12):
        d.type("aban")
    d.screen("confirm", "Checksum invalid")
    d.key("n")  # fix it
    st = d.screen("mnemonic_input")
    d.key("bs")  # reopen the last word
    d.key("bs", 6)
    d.type("abou")
    d.toast("Checksum OK")
    d.screen("entry_edit")
    d.key("esc")  # dirty -> discard?
    d.screen("confirm", "Discard changes?")
    d.key("y")
    st = d.screen("home")
    check(st["entries"] == 2, f"discarded entry was saved: {st}")


def t07_totp_validation_and_code(d):
    d.cmd(f"settime {int(time.time())}")
    new_entry(d, "2FA (TOTP)")
    d.key("down")  # Issuer
    d.key("enter")
    d.screen("text_input", "Issuer")
    d.type("GitHub")
    d.key("enter")
    d.key("down")  # Secret
    d.key("enter")
    d.screen("text_input", "Secret (base32)")
    d.type("not-base32!")
    d.key("enter")
    save_entry(d)
    d.toast("not valid base32")
    d.screen("entry_edit")
    d.key("up", 3)  # Save -> Secret
    d.key("enter")
    d.screen("text_input", "Secret (base32)")
    d.key("bs", 11)
    d.type(TOTP_SECRET)
    d.key("enter")
    save_entry(d)
    d.screen("entry_view")
    go_home(d)
    d.key("tab")
    d.menu_pick("2FA codes")
    st = d.screen("totp_list")
    title, code, t = info(st)["codes"].split("|")[0].split(":")
    check(title == "GitHub", f"default title should be the issuer, got {title}")
    check(code == totp(TOTP_SECRET, int(t)), f"device code {code} != expected {totp(TOTP_SECRET, int(t))}")
    d.key("esc")


def t08_password_generator_to_entry(d):
    go_home(d)
    d.key("tab")
    d.menu_pick("Password generator")
    st = d.screen("pwgen")
    v1 = info(st)["value"]
    check(len(v1) == 20, f"default length 20, got {v1!r}")
    d.key("g")
    v2 = d.wait(lambda s: info(s)["value"] != v1, "regenerate")
    v2 = info(v2)["value"]
    d.key("tab")
    d.screen("entry_edit")
    set_title(d, "Email")
    save_entry(d)
    d.screen("entry_view")
    pw = [e for e in d.dump() if e["title"] == "Email"]
    check(pw and pw[0]["fields"]["5"] == v2, f"generated password not stored: {pw}")


def t09_search_and_delete(d):
    st = go_home(d)
    before = st["entries"]
    d.type("Shop")
    st = d.wait(lambda s: info(s)["rows"] == "1", "search filters to one row")
    d.key("enter")
    d.screen("entry_view")
    d.key("tab")
    d.menu_pick("Delete")
    d.screen("confirm", "Delete secret?")
    d.key("y")
    st = d.screen("home")
    check(st["entries"] == before - 1, f"entry not deleted: {st}")
    check(all(e["title"] != "Shopping" for e in d.dump()), "deleted entry still present")
    d.key("esc")  # clear the search query (does not lock)
    st = d.screen("home")
    check(info(st)["query"] == "", "query not cleared")


def t10_lock_and_navigation(d):
    st = d.lock()
    check(not st["open"] and st["entries"] == 0 and st["depth"] == 1,
          f"lock must wipe data and leave only the unlock screen: {st}")
    d.key("esc")
    d.key("backtick")  # a passphrase character on the unlock screen, not "back"
    st = d.screen("unlock")
    check(info(st)["len"] == "1", "backtick should be typed into the passphrase")
    d.key("esc")
    st = d.wait(lambda s: info(s)["len"] == "0", "esc clears the passphrase")
    d.type("wrong passphrase")
    d.key("enter")
    st = d.wait(lambda s: "Wrong" in info(s).get("error", ""), "wrong passphrase error", 30)
    check(st["fails"] == 1 and not st["open"], f"bad state after wrong attempt {st}")
    st = d.unlock(P1)
    check(st["fails"] == 0 and st["depth"] == 1, f"unlock must reset counter and history: {st}")


def t11_persistence_across_reboot(d):
    before = d.dump()
    d.restart_cmd("reboot")
    d.screen("unlock")
    d.unlock(P1)
    after = d.dump()
    check(before == after, "vault contents changed across reboot")


def t12_g0_locks(d):
    d.screen("home")
    d.key("tab")
    d.menu_pick("Settings")
    d.screen("settings")
    d.cmd("g0")
    st = d.screen("unlock")
    check(st["depth"] == 1 and not st["open"], f"G0 must lock from any screen: {st}")
    d.unlock(P1)


def t13_autolock_dims_then_locks(d):
    st = d.screen("home")
    d.cmd("idle 111000")  # default auto-lock 120 s, dims 10 s before
    d.wait(lambda s: s["dimmed"], "screen dims before locking")
    d.type("x")  # the waking key press is swallowed
    st = d.wait(lambda s: not s["dimmed"], "key wakes the screen")
    check(info(st)["query"] == "", "waking key must not be typed")
    d.cmd("idle 121000")
    st = d.screen("unlock")
    check(not st["open"], "auto-lock must close the session")
    d.unlock(P1)


def t14_change_passphrase(d):
    go_home(d)
    d.key("tab")
    d.menu_pick("Settings")
    d.screen("settings")
    d.key("down", 3)
    d.key("enter")
    d.screen("text_input", "Current passphrase")
    d.key("enter")  # empty: refused before any KDF runs
    d.toast("At least 1 character")
    d.screen("text_input", "Current passphrase")
    d.type("not it at all")
    d.key("enter")
    st = d.toast("Wrong passphrase", timeout=30)
    check(st["fails"] == 1, f"wrong current passphrase must count: {st}")
    d.key("enter")
    d.screen("text_input", "Current passphrase")
    d.type(P1)
    d.key("enter")
    st = d.screen("text_input", "New passphrase", timeout=30)
    check(st["fails"] == 0, f"right current passphrase must reset the counter: {st}")
    d.type(P2)
    d.key("enter")
    d.type(P2)
    d.key("enter")
    # Success offers to compact (old copies still open with the old passphrase).
    st = d.screen("confirm", "Passphrase changed", timeout=40)
    check("OLD passphrase" in st.get("info", ""), f"compact offer missing: {st}")
    d.key("n")  # compacting is covered on its own (needs an SD card)
    d.screen("settings")
    d.cmd("g0")
    d.screen("unlock")
    d.type(P1)
    d.key("enter")
    d.wait(lambda s: "Wrong" in info(s).get("error", ""), "old passphrase rejected", 30)
    st = d.unlock(P2)
    check(st["entries"] == 3, f"entries lost after passphrase change: {st}")


def t15_power_cut_mid_attempt_still_counts(d):
    d.lock()
    d.type("wrong again")
    d.key("enter")
    time.sleep(0.5)  # Argon2 is running (~2 s)
    d.hard_reset()
    st = d.screen("unlock")
    check(st["fails"] == 1, f"attempt must be recorded before the check: {st}")
    st = d.unlock(P2)
    check(st["fails"] == 0, "success resets the counter")


def t16_ten_wrong_passphrases_wipe(d):
    assert_vault_stored(d)  # so the wiped check below can tell the two apart
    d.lock()
    for i in range(1, 11):
        st = d.wait(lambda s: s["screen"] != "unlock" or int(info(s)["wait_ms"]) == 0,
                    f"delay before attempt {i}", 30)
        if st["screen"] != "unlock":
            break
        d.type(f"guess number {i}")
        d.key("enter")
        if i == 10:
            break
        st = d.wait(lambda s: s["fails"] == i, f"failure {i} recorded", 30)
        if i >= 8:  # long delays: input must be ignored while waiting
            check(int(info(st)["wait_ms"]) > 0, f"expected a delay after {i} failures")
            d.type("x")
            st = d.state()
            check(info(st)["len"] == "0", "keys must be ignored during the delay")
    # The 10th failure erases the vault and reboots.
    time.sleep(3)
    d.close()
    d.connect(timeout=40)
    st = d.screen("setup", timeout=30)
    check(st["fails"] == 0, f"counter must be cleared after the wipe: {st}")
    assert_wiped(d)


def t17_erase_from_settings(d):
    t02_setup_validation_and_create(d)
    assert_vault_stored(d)
    d.key("tab")
    d.menu_pick("Settings")
    d.screen("settings")
    d.key("up")  # wraps to the last row: Erase vault
    d.key("enter")
    d.screen("text_input", "Erase vault")
    d.type("erase")  # wrong case -> refused
    d.key("enter")
    d.toast("Not erased")
    d.key("enter")
    d.screen("text_input", "Erase vault")
    d.type("ERASE")
    d.restart_cmd("key enter")  # erases, reboots and reconnects
    st = d.screen("setup", timeout=40)
    check(st["fails"] == 0, f"counter not reset: {st}")
    assert_wiped(d)


def t18_tab_never_saves(d):
    # Regression #31: TAB walked menus and saved empty entries.
    d.screen("setup")
    d.key("tab", 10)
    st = d.state()
    check(not st["open"] and st["fails"] == 0, f"TAB on setup changed state: {st}")
    for _ in range(4):
        if d.state()["screen"] == "setup":
            break
        d.key("esc")
    t02_setup_validation_and_create(d)

    d.key("tab", 10)
    st = d.screen("menu", "Menu")
    check(st["entries"] == 0, f"TAB from home saved something: {st}")
    go_home(d)

    new_entry(d, "Note")
    d.key("tab", 5)
    st = d.screen("entry_edit")
    check(info(st)["sel"] == "2", f"TAB should move to the Save row: {st}")
    check(d.dump() == [], "TAB in the editor saved an entry")
    d.key("enter")
    d.toast("Text is empty")
    d.key("esc")
    st = d.screen("home")
    check(st["entries"] == 0, f"empty note was saved: {st}")

    new_entry(d, "Password")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Username")
    d.type("alice")
    d.key("enter")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Password")
    d.type("hunter22")
    d.key("enter")
    set_title(d, "Bank")
    save_entry(d)
    d.screen("entry_view")
    d.key("e")
    d.screen("entry_edit")
    d.key("down", 2)  # Password: the defining field cannot be cleared
    d.key("enter")
    d.screen("text_input", "Password")
    d.key("bs", 8)
    d.key("enter")
    d.toast("can't be empty")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"clearing the password must be refused: {st}")
    d.key("up")  # Username: clearing asks first
    d.key("enter")
    d.screen("text_input", "Username")
    d.key("bs", 5)
    d.key("enter")
    d.screen("confirm", "Clear Username?")
    d.key("n")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"declined clear must not change the draft: {st}")
    d.key("down")  # Password: G asks before replacing it
    d.key("g")
    d.screen("confirm", "Replace password?")
    d.key("n")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"declined replace must not change the draft: {st}")
    d.key("esc")
    d.screen("entry_view")
    bank = [e for e in d.dump() if e["title"] == "Bank"]
    check(bank and bank[0]["fields"]["5"] == "hunter22" and bank[0]["fields"]["4"] == "alice",
          f"entry changed: {bank}")

    d.key("tab")
    st = d.screen("menu", "Secret")
    items = info(st)["items"].split("|")
    d.key(str(items.index("Delete") + 1))
    st = d.screen("menu", "Secret")
    check(info(st)["sel"] == str(items.index("Delete")), f"digit should only select Delete: {st}")
    d.key("esc")
    d.screen("entry_view")
    check(len(d.dump()) == 1, "digit hotkey deleted the entry")

    go_home(d)
    st = d.lock()
    fails = st["fails"]
    d.key("tab", 10)
    st = d.screen("menu", "Options")
    check(st["fails"] == fails and not st["open"], f"TAB on the lock screen: {st}")
    d.key("esc")
    d.unlock(P1)


def t19_sd_without_card(d):
    if SD_CARD:
        raise Skip("card inserted (--sd)")
    before = d.dump()
    open_setting(d, ROW_BACKUP)
    d.screen("message", "No SD card", timeout=15)
    d.key("enter")
    open_setting(d, ROW_RESTORE)
    d.screen("message", "No SD card", timeout=15)
    d.key("enter")
    # Compact needs its SD safety copy first: without a card nothing changes.
    open_setting(d, ROW_COMPACT)
    d.screen("confirm", "Compact vault")
    d.key("y")
    st = d.screen("message", "Compact vault", timeout=15)
    check("Nothing was changed" in info(st)["msg"], f"compact without card: {st}")
    d.key("enter")
    check(d.dump() == before, "vault changed by a refused compact")
    d.cmd("g0")  # flash still holds the vault, so locking is allowed
    d.screen("unlock")
    d.unlock(P1)


def t20_sd_backup_and_restore(d):
    need_card()
    before = d.dump()
    open_setting(d, ROW_BACKUP)
    d.screen("text_input", "Backup passphrase", timeout=15)
    d.type(P3)
    d.key("enter")
    d.screen("text_input", "Confirm passphrase")
    d.type(P3)
    d.key("enter")
    st = d.screen("message", "Backup saved", timeout=60)
    name = backup_name_in(st)
    d.key("enter")

    # Change the vault so the restore is observable.
    new_entry(d, "Note")
    set_title(d, "After backup")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    d.type("temp")
    d.key("tab")
    d.screen("entry_edit")
    save_entry(d)
    d.screen("entry_view")
    check(len(d.dump()) == len(before) + 1, "note not added")

    # Restore while unlocked: newest backup first, wrong passphrase not counted.
    open_setting(d, ROW_RESTORE)
    st = d.screen("menu", "Choose backup", timeout=15)
    check(info(st)["items"].split("|")[0] == name, f"newest backup not first: {st}")
    d.key("enter")
    empty_backup_passphrase_refused(d)  # unlocked
    d.type("not the backup pass")
    d.key("enter")
    st = d.screen("message", "Restore failed", timeout=30)
    check(st["fails"] == 0, f"unlocked restore guess must not count: {st}")
    d.key("enter")
    d.screen("settings")  # picking a backup closed the list
    d.key("enter")  # Restore from SD again (row still selected)
    d.screen("menu", "Choose backup", timeout=15)
    d.key("enter")
    d.screen("text_input", "Backup passphrase")
    d.type(P3)
    d.key("enter")
    d.screen("confirm", "Replace vault?", timeout=30)
    d.key("y")
    d.screen("confirm", "Restored", timeout=30)  # offers to compact
    d.key("n")
    d.screen("home")
    check(d.dump() == before, "restore did not bring back the backed-up vault")
    d.lock()
    d.unlock(P3)  # the restored vault opens with the backup passphrase

    # #43: TAB on the setup screen reads the card (this used to hang).
    d.restart_cmd("factory")
    d.screen("setup")
    d.key("tab")
    st = d.screen("menu", "Choose backup", timeout=15)
    check(info(st)["items"].split("|")[0] == name, f"newest backup not first: {st}")
    d.key("enter")
    empty_backup_passphrase_refused(d)  # locked: nothing is open yet
    d.type(P3)
    d.key("enter")
    d.screen("confirm", "Replace vault?", timeout=30)
    d.key("y")
    d.screen("confirm", "Restored", timeout=30)
    d.key("n")
    st = d.screen("home")
    check(d.dump() == before and st["fails"] == 0, f"restore on setup failed: {st}")


def t21_sd_compact_with_safety_copy(d):
    need_card()
    before = d.dump()
    open_setting(d, ROW_COMPACT)
    d.screen("confirm", "Compact vault")
    d.key("y")
    st = d.screen("message", "Compact vault", timeout=60)
    check("opens with your vault passphrase" in info(st)["msg"], f"compact failed: {st}")
    copy = backup_name_in(st)
    d.key("enter")
    d.restart_cmd("reboot")
    d.unlock(P3)
    check(d.dump() == before, "vault changed by compact")
    # The safety copy is the newest backup and opens with the vault passphrase.
    open_setting(d, ROW_RESTORE)
    st = d.screen("menu", "Choose backup", timeout=15)
    check(info(st)["items"].split("|")[0] == copy, f"safety copy not newest: {st}")
    d.key("enter")
    d.screen("text_input", "Backup passphrase")
    d.type(P3)
    d.key("enter")
    d.screen("confirm", "Replace vault?", timeout=30)
    d.key("n")  # verified; no need to replace
    d.screen("settings")
    go_home(d)


def t22_confirm_and_validation(d):
    # Regression #38: Esc loses typed text, blank seeds, visible TOTP secret,
    # spurious dirty flag, made-up clock default.
    st = d.state()
    if st["screen"] == "unlock":
        d.unlock(P3 if SD_CARD else P1)  # t20 restores a backup made with P3
    go_home(d)
    stored = len(d.dump())
    memo = f"Memo{int(time.time()) % 100000}"  # unique, so reruns find only this one

    # Esc in an edited text input asks; an untouched one closes at once.
    new_entry(d, "Note")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    d.key("esc")
    d.screen("entry_edit")  # nothing typed: no question
    d.key("enter")
    d.screen("text_input", "Text")
    d.type("ab")
    d.key("bs", 2)
    d.key("esc")
    d.screen("entry_edit")  # typed and erased: same as the start, no question
    d.key("enter")
    d.screen("text_input", "Text")
    d.type("keep me")
    d.key("esc")
    d.screen("confirm", "Discard changes?")
    d.key("n")
    st = d.screen("text_input", "Text")
    check(info(st)["len"] == "7", f"declining must keep the text: {st}")
    d.key("esc")
    d.screen("confirm", "Discard changes?")
    d.key("y")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"discarded text must not dirty the draft: {st}")
    check(len(d.dump()) == stored, "discarding must not save anything")

    # Saving the same value is not a change.
    d.key("enter")
    d.screen("text_input", "Text")
    d.type("x")
    d.key("tab")
    d.screen("entry_edit")
    set_title(d, memo)
    save_entry(d)
    d.screen("entry_view")
    d.key("e")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"fresh editor must be clean: {st}")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    d.key("tab")  # same text
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"re-saving the same text marked it dirty: {st}")
    d.key("up")
    d.key("enter")
    d.screen("text_input", "Title")
    d.key("enter")  # same title
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"re-entering the same title marked it dirty: {st}")
    d.key("esc")
    d.screen("entry_view")

    # Free-text seed must contain a visible character.
    new_entry(d, "Seed phrase")
    d.key("down")
    d.key("enter")
    d.menu_pick("Free text (non-BIP39)")
    d.screen("text_input", "Seed words")
    d.type("   ")
    d.key("tab")
    d.toast("empty")
    d.screen("text_input", "Seed words")
    d.key("esc")
    d.screen("confirm", "Discard changes?")
    d.key("y")
    d.screen("entry_edit")

    # The TOTP secret is masked until OPT.
    new_entry(d, "2FA (TOTP)")
    d.key("down", 2)  # Secret
    d.key("enter")
    d.screen("text_input", "Secret (base32)")
    d.type("JBSWY3DP")
    st = d.state()
    check(info(st)["reveal"] == "0", f"TOTP secret input must start masked: {st}")
    d.key("opt")
    st = d.state()
    check(info(st)["reveal"] == "1", f"OPT should reveal the secret: {st}")
    d.key("esc")  # a typed secret is worth a question, masked or not
    d.screen("confirm", "Discard changes?")
    d.key("y")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "0", f"discarded secret must not dirty the draft: {st}")
    d.key("down")  # Digits: 6 -> 8 -> 6 is no change
    d.key("enter")
    st = d.state()
    check(info(st)["dirty"] == "1", f"changing Digits should dirty the draft: {st}")
    d.key("enter")
    st = d.state()
    check(info(st)["dirty"] == "0", f"Digits back to 6 must be clean again: {st}")
    d.key("esc")
    d.screen("home")

    # A generated password is unsaved: Esc must ask before dropping it.
    d.key("tab")
    d.menu_pick("Password generator")
    d.screen("pwgen")
    d.key("tab")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "1", f"generated password must start dirty: {st}")
    d.key("esc")
    d.screen("confirm", "Discard changes?")
    d.key("y")
    d.screen("home")

    # A physically held key must not repeat into the next screen: Y on the
    # delete confirm, held well past the repeat delay, then home's search box.
    d.type(memo)
    d.wait(lambda s: info(s)["rows"] == "1", f"search finds {memo}")
    d.key("enter")
    d.screen("entry_view")
    d.key("tab")
    d.menu_pick("Delete")
    d.screen("confirm", "Delete secret?")
    d.keys("y")
    try:
        st = d.screen("home")
        time.sleep(1.5)  # > 450 ms repeat delay
    finally:
        d.keys()
    st = d.state()
    check(st["entries"] == stored, f"held Y should have deleted {memo}: {st}")
    check(info(st)["query"] == memo, f"held Y repeated into the next screen: {st}")
    d.key("esc")  # clear the search
    d.screen("home")

    # Set clock: starts empty when unset, refuses old dates and trailing text,
    # and a refused value comes back for fixing.
    d.cmd("settime 0")
    try:
        d.key("tab")
        d.menu_pick("2FA codes")
        d.screen("totp_list")
        d.key("t")
        st = d.screen("text_input", "Set clock (UTC)")
        check(info(st)["len"] == "0", f"clock input must not be pre-filled when unset: {st}")
        d.type("2024-12-31 23:59")
        d.key("enter")
        d.toast("before 2025")
        st = d.screen("text_input", "Set clock (UTC)")
        check(info(st)["len"] == "16", f"a refused date must come back for fixing: {st}")
        d.key("bs", 16)
        d.type("2026-01-01 12:00xyz")
        d.key("enter")
        d.toast("Invalid")
        st = d.screen("text_input", "Set clock (UTC)")
        check(info(st)["len"] == "19", f"refused text must come back for fixing: {st}")
        d.key("bs", 3)
        d.key("enter")
        d.toast("Clock set")
        d.screen("totp_list")
        d.key("esc")
        go_home(d)
    finally:
        d.cmd(f"settime {int(time.time())}")

    # With the clock set, Set clock starts from the real time (YYYY-MM-DD HH:MM).
    d.key("tab")
    d.menu_pick("2FA codes")
    d.screen("totp_list")
    d.key("t")
    st = d.screen("text_input", "Set clock (UTC)")
    check(info(st)["len"] == "16", f"clock input must start from the current time: {st}")
    d.key("esc")  # untouched: closes without a question
    d.screen("totp_list")
    d.key("esc")
    go_home(d)


def t23_secret_hygiene(d):
    # Regression #39: ENTER reveals one secret row only, reveals time out
    # (4 s in the e2e build) and hide on resume and dim, and locked screens
    # drop typed input when idle.
    pw = P3 if SD_CARD else P1  # see t22
    if d.state()["screen"] == "unlock":
        d.unlock(pw)
    go_home(d)
    title = f"Hyg{int(time.time()) % 100000}"
    new_entry(d, "Password")
    set_title(d, title)
    d.key("down")  # Username
    d.key("enter")
    d.screen("text_input", "Username")
    d.type("someone")
    d.key("enter")
    d.screen("entry_edit")
    d.key("down")  # Password
    d.key("enter")
    d.screen("text_input", "Password")
    d.type("pw-secret-1")
    d.key("enter")
    d.screen("entry_edit")
    save_entry(d)
    st = d.screen("entry_view")
    check(info(st)["reveal_all"] == "0" and info(st)["row"] == "-1", f"starts masked: {st}")

    # ENTER on Username (not a secret): nothing is revealed.
    d.key("enter")
    st = d.state()
    check(info(st)["reveal_all"] == "0" and info(st)["row"] == "-1",
          f"ENTER on a non-secret row must reveal nothing: {st}")

    # ENTER on Password reveals only that row, then it hides by itself.
    d.key("down")
    d.key("enter")
    st = d.state()
    check(info(st)["row"] == "1" and info(st)["reveal_all"] == "0", f"one row only: {st}")
    d.wait(lambda s: info(s)["row"] == "-1", "revealed row hides after the timeout", timeout=10)

    # R reveals all, hidden again by the timeout.
    d.key("r")
    check(info(d.state())["reveal_all"] == "1", "R should reveal all")
    d.wait(lambda s: info(s)["reveal_all"] == "0", "R reveal hides after the timeout", timeout=10)

    # Coming back from another screen starts masked.
    d.key("r")
    d.key("e")
    d.screen("entry_edit")
    d.key("esc")
    st = d.screen("entry_view")
    check(info(st)["reveal_all"] == "0", f"reveal must not survive leaving the screen: {st}")

    # Dimming hides it too.
    d.key("r")
    d.cmd("idle 111000")  # default auto-lock 120 s, dims 10 s before
    st = d.wait(lambda s: s["dimmed"], "screen dims")
    check(info(st)["reveal_all"] == "0", f"dim must hide revealed secrets: {st}")
    d.key("down")  # the waking key press is swallowed
    d.wait(lambda s: not s["dimmed"], "key wakes the screen")

    # Holding SPACE peeks for at most the reveal time, and dimming ends a
    # peek until the key is released (a key stuck down in a pocket).
    d.cmd("hold space 1")
    try:
        d.wait(lambda s: info(s)["held"] == "1", "peek while held")
        d.wait(lambda s: info(s)["held"] == "0", "peek ends after the reveal time", timeout=10)
        d.cmd("hold space 0")
        d.wait(lambda s: info(s)["held"] == "0", "released")
        d.cmd("hold space 1")
        d.wait(lambda s: info(s)["held"] == "1", "a new press peeks again")
        d.cmd("idle 111000")
        st = d.wait(lambda s: s["dimmed"], "screen dims while held")
        check(info(st)["held"] == "0", f"dim must end the peek: {st}")
        d.key("down")  # wakes
        st = d.wait(lambda s: not s["dimmed"], "wake")
        check(info(st)["held"] == "0", f"peek stays off until the key is released: {st}")
    finally:
        d.cmd("hold space 0")

    # Delete the test entry again.
    d.key("tab")
    d.menu_pick("Delete")
    d.screen("confirm", "Delete secret?")
    d.key("y")
    go_home(d)

    # The generator hides its password when the screen dims.
    d.key("tab")
    d.menu_pick("Password generator")
    st = d.screen("pwgen")
    check(info(st)["hidden"] == "0", f"generator starts visible: {st}")
    d.cmd("idle 111000")
    st = d.wait(lambda s: s["dimmed"], "generator dims")
    check(info(st)["hidden"] == "1", f"dim must hide the generated password: {st}")
    d.key("down")  # swallowed: wakes
    d.wait(lambda s: not s["dimmed"], "wake")
    d.key("down")
    d.wait(lambda s: info(s)["hidden"] == "0", "next key shows it again")
    d.key("esc")
    go_home(d)

    # Brightness never wraps below its minimum to full brightness.
    d.key("tab")
    d.menu_pick("Settings")
    d.screen("settings")
    d.key("down")  # Brightness
    d.key("left", 8)
    st = d.state()
    check(st["bright"] == 20, f"brightness must stop at 20: {st}")
    d.key("right")
    check(d.state()["bright"] == 45, "brightness steps up again")
    go_home(d)

    # Locked: a typed (and revealed) passphrase is dropped when idle.
    d.lock()
    d.type("half typed")
    d.key("opt")
    check(info(d.state())["len"] == "10", "passphrase typed")
    d.cmd("idle 121000")
    st = d.wait(lambda s: s["dimmed"], "locked screen dims when idle")
    check(info(st)["len"] == "0", f"idle must wipe the typed passphrase: {st}")
    d.key("down")  # swallowed: wakes the screen
    d.wait(lambda s: not s["dimmed"], "key wakes the locked screen")

    # Open prompts above the lock screen are closed too.
    d.key("tab")
    d.menu_pick("Erase vault")
    d.screen("text_input", "Erase vault")
    d.type("ERA")
    d.cmd("idle 121000")
    st = d.wait(lambda s: s["dimmed"], "prompt closes when idle")
    check(st["screen"] == "unlock" and st["depth"] == 1, f"back on the lock screen: {st}")
    d.key("down")
    d.wait(lambda s: not s["dimmed"], "wake")
    d.unlock(pw)


def t24_tab_cascade_and_clear_field(d):
    # Regression #31: TAB must never create, change or delete data on any
    # screen, and clearing a stored optional field asks first and sticks.
    pw = P3 if SD_CARD else P1
    if d.state()["screen"] == "unlock":
        d.unlock(pw)
    go_home(d)
    run = int(time.time()) % 100000  # unique titles, so reruns find only this run's
    note_t, pw_t, totp_t = f"TabNote{run}", f"ClearMe{run}", f"Acme{run}"

    def storm(what, times=8):
        before = d.dump()
        fails = d.state()["fails"]
        d.key("tab", times)
        st = d.state()  # still answering: nothing crashed or restarted
        check(st["open"] and st["fails"] == fails, f"TAB on {what} changed state: {st}")
        check(d.dump() == before, f"TAB on {what} created, changed or deleted data")
        go_home(d)

    # Menus and tools.
    d.key("tab")
    d.screen("menu", "Menu")
    storm("the main menu")
    d.key("tab")
    d.menu_pick("Settings")
    d.screen("settings")
    storm("settings")
    d.key("tab")
    d.menu_pick("2FA codes")
    d.screen("totp_list")
    storm("the 2FA list")
    d.key("tab")
    d.menu_pick("Password generator")
    d.screen("pwgen")
    storm("the password generator")

    # Every editor, then the inputs reached from them.
    for label in ("Seed phrase", "Password", "2FA (TOTP)", "Note"):
        new_entry(d, label)
        storm(f"the {label} editor")
    new_entry(d, "Note")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    storm("a text input")
    new_entry(d, "Seed phrase")
    d.key("down")
    d.key("enter")
    d.menu_pick("Type 12 words")
    d.screen("mnemonic_input")
    storm("the seed word input")

    # A saved entry: its viewer and its action menu.
    new_entry(d, "Note")
    set_title(d, note_t)
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    d.type("tab test")
    d.key("tab")  # multi-line: Enter is a newline, TAB is done
    d.screen("entry_edit")
    save_entry(d)
    d.screen("entry_view")
    storm("the entry viewer")
    find_one(d, note_t)

    # Clearing a stored optional field: asks, and a yes sticks across a reboot.
    new_entry(d, "Password")
    set_title(d, pw_t)
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Username")
    d.type("alice")
    d.key("enter")
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Password")
    d.type("hunter22")
    d.key("enter")
    save_entry(d)
    d.screen("entry_view")
    d.key("e")
    d.screen("entry_edit")
    d.key("down")  # Username
    d.key("enter")
    d.screen("text_input", "Username")
    d.key("bs", 5)
    d.key("enter")
    d.screen("confirm", "Clear Username?")
    d.key("y")
    st = d.screen("entry_edit")
    check(info(st)["dirty"] == "1", f"a confirmed clear must change the draft: {st}")
    save_entry(d)
    d.screen("entry_view")

    new_entry(d, "2FA (TOTP)")
    d.key("down")  # Issuer
    d.key("enter")
    d.screen("text_input", "Issuer")
    d.type(totp_t)  # the title defaults to the issuer
    d.key("enter")
    d.key("down")  # Secret
    d.key("enter")
    d.screen("text_input", "Secret (base32)")
    d.type(TOTP_SECRET)
    d.key("enter")
    save_entry(d)
    d.screen("entry_view")
    d.key("e")
    d.screen("entry_edit")
    d.key("down")  # Issuer
    d.key("enter")
    d.screen("text_input", "Issuer")
    d.key("bs", len(totp_t))
    d.key("enter")
    d.screen("confirm", "Clear Issuer?")
    d.key("y")
    save_entry(d)
    d.screen("entry_view")

    def cleared():
        me = find_one(d, pw_t)
        check(me["fields"].get("4", "") == "" and me["fields"]["5"] == "hunter22",
              f"username must be cleared, password kept: {me}")
        acme = find_one(d, totp_t)
        check(acme["fields"].get("11", "") == "" and acme["fields"]["8"] == TOTP_SECRET,
              f"issuer must be cleared, secret kept: {acme}")

    cleared()
    d.restart_cmd("reboot")
    d.unlock(pw)
    cleared()

    # Leave the vault as this scenario found it.
    for title in (note_t, pw_t, totp_t):
        delete_entry(d, title)
    check(not any(e["title"] in (note_t, pw_t, totp_t) for e in d.dump()),
          "test entries left behind")


REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def test_partition(label):
    """(offset, size) of a partition in the test builds' table."""
    with open(os.path.join(REPO, "partitions_test.csv")) as f:
        for line in f:
            cols = [c.strip() for c in line.split("#", 1)[0].split(",")]
            if cols[0] == label:
                return int(cols[3], 0), int(cols[4], 0)
    raise Fail(f"no {label!r} partition in partitions_test.csv")


def find_esptool():
    """The command that runs PlatformIO's esptool: this Python if it has
    esptool's dependencies, else PlatformIO's own. Skips when neither works."""
    core = os.environ.get("PLATFORMIO_CORE_DIR", os.path.expanduser("~/.platformio"))
    pythons = [sys.executable]
    try:
        r = subprocess.run(["pio", "system", "info", "--json-output"],
                           capture_output=True, text=True, timeout=60)
        info_ = json.loads(r.stdout)  # {"core_dir": {"title": ..., "value": ...}, ...}
        core = info_["core_dir"]["value"]
        pythons.append(info_["python_exe"]["value"])
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired):
        pass  # no pio on PATH: try this Python only
    tool = os.path.join(core, "packages", "tool-esptoolpy", "esptool.py")
    if not os.path.exists(tool):
        raise Skip(f"esptool.py not found at {tool}")
    for py in pythons:
        try:
            r = subprocess.run([py, tool, "version"], capture_output=True, timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            continue
        if r.returncode == 0:
            return [py, tool]
    raise Skip(f"no Python here can run {tool} (missing dependencies)")


def esptool_read(esptool, port, offset, size, out):
    """Dump flash. esptool resets the device into the bootloader over USB and
    back into the app afterwards."""
    try:
        r = subprocess.run(
            esptool + ["--chip", "esp32s3", "--port", port, "--baud", "921600",
                       "read_flash", hex(offset), hex(size), out],
            capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        raise Fail(f"esptool read_flash {hex(offset)} timed out")
    if r.returncode != 0:
        raise Fail(f"esptool read_flash {hex(offset)} failed: {r.stdout[-300:]} {r.stderr[-300:]}")


def secrets(d, min_len=4, extra=()):
    """Everything that must never show up in flash or on the serial port: the
    passphrases, the TOTP secret and every string of `min_len`+ characters this
    run typed, plus `extra`. The search ignores case, so each letter matches 2 of
    256 byte values: a 4-letter string turns up by chance about once per 256 MiB.
    That is fine for the few KiB of serial output, not for a 1 MiB flash dump
    searched for dozens of strings, which uses 8+ (once per 2^56 bytes)."""
    return {s for s in {P1, P2, P3, TOTP_SECRET} | d.typed | set(extra) if len(s) >= min_len}


def t25_flash_holds_no_plaintext(d):
    """Hardware checklist (#17): `esptool read_flash` of the vault partition
    shows no plaintext. Stores a canary note, dumps the raw vault partition
    and the NVS partition over USB and searches them for every secret."""
    esptool = find_esptool()  # before touching the vault: a Skip leaves nothing behind
    vault_at, vault_size = test_partition("cvtvault")
    nvs_at, nvs_size = test_partition("nvs")
    pw = P3 if SD_CARD else P1
    if d.state()["screen"] == "unlock":
        d.unlock(pw)
    go_home(d)
    run = int(time.time()) % 100000
    title, body = f"CanaryTitle{run}", f"CanaryBody{run}Secret"
    new_entry(d, "Note")
    set_title(d, title)
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")
    d.type(body)
    d.key("tab")
    d.screen("entry_edit")
    save_entry(d)
    d.screen("entry_view")

    passed = False
    try:
        check(find_one(d, title)["fields"]["1"] == body, "canary note not saved")
        # Whole stored values too: short typed pieces (seed word prefixes,
        # "milk") are too short to search for, the values they built are not.
        stored = [v for e in d.dump() for v in [e["title"], *e["fields"].values()]]
        st = assert_vault_stored(d)
        d.close()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                vault, nvs = os.path.join(tmp, "vault.bin"), os.path.join(tmp, "nvs.bin")
                esptool_read(esptool, d.port, vault_at, vault_size, vault)
                esptool_read(esptool, d.port, nvs_at, nvs_size, nvs)
                with open(vault, "rb") as f:
                    vdata = f.read()
                with open(nvs, "rb") as f:
                    ndata = f.read()
        finally:
            d.connect(timeout=40)

        check(len(vdata) == vault_size and len(ndata) == nvs_size, "short flash read")
        hits = vdata.count(b"CVLT")
        check(hits == st["magic_hits"] and hits >= 1,
              f"the dump must match what the device sees: {hits} vs {st['magic_hits']}")
        # Positive control: the NVS dump holds readable names, so the search
        # below does find text when there is some.
        check(b"cvt_guard" in ndata, "NVS dump is not the device's NVS (control string missing)")
        used = vdata.translate(None, b"\xff")
        check(len(used) > 1000, f"the vault partition looks empty ({len(used)} bytes used)")
        for name, data in (("vault partition", vdata), ("NVS partition", ndata)):
            folded = data.lower()
            for s in sorted(secrets(d, min_len=8, extra=stored)):
                check(s.lower().encode() not in folded, f"plaintext {s!r} found in the {name}")
        # Beyond known strings: ciphertext has no long printable runs (random
        # bytes give one of 24+ about once in 20,000 MiB). The names LittleFS
        # keeps in clear ("littlefs", the vault file names) are shorter.
        runs = {m.group() for m in re.finditer(rb"[\x20-\x7e]{24,}", vdata)}
        check(not runs, f"readable text in the vault partition: {sorted(runs)[:5]}")
        passed = True
    finally:
        # Leave the vault as this scenario found it. A cleanup failure only
        # fails the scenario when nothing else did (and must not hide that).
        try:
            if d.state()["screen"] == "unlock":  # the dump rebooted the device
                d.unlock(pw)
            delete_entry(d, title)
        except Exception as e:
            if passed:
                raise
            print(f"      (cleanup failed too: {e})")


def t26_serial_output_is_silent(d):
    """Hardware checklist (#17): the serial monitor shows nothing sensitive.
    Every line the device printed in this run that is not a reply to the
    harness (boot output, stray prints, driver errors) is checked against
    every secret. Reboots and unlocks itself, so it also has something to
    check when run alone (-k t26). Output from before the host reopens the
    port after a reset is lost on native USB and cannot be checked."""
    pw = P3 if SD_CARD else P1
    before = len(d.noise)
    d.restart_cmd("reboot")
    d.unlock(pw)
    d.lock()
    d.unlock(pw)
    time.sleep(1)
    d.cmd("ping")  # read what arrived meanwhile
    check(any(l == "CVTEST READY" for l in d.noise[before:]),
          f"boot output after the reboot was not captured: {d.noise[before:]}")
    needles = secrets(d)
    for line in d.noise:
        folded = line.lower()
        for s in needles:
            check(s.lower() not in folded, f"serial output contains {s!r}: {line!r}")
    # The device must not chat: boot output (about 8 lines a boot) is fine, a
    # line per key or per unlock would show up as a flood.
    boots = max(1, d.noise.count("CVTEST READY"))
    check(len(d.noise) <= 20 * boots,
          f"{len(d.noise)} stray serial lines for {boots} boots: {d.noise[:5]}")
    print(f"      ({len(d.noise)} device lines checked against {len(needles)} secrets, "
          f"{len(d.fragments)} reply fragments skipped: {sorted(set(d.noise))[:4]})")


def t27_screenshot_hook(d):
    """The `shot` command (tools/screenshots.py) returns the whole screen,
    whichever it is, and leaves it as it was."""
    before = d.state()
    w, h, rows = d.shot()
    check((w, h) == (240, 135), f"unexpected size {w}x{h}")
    colours = {row[i:i + 4] for row in rows for i in range(0, len(row), 4)}
    check(len(colours) >= 4, f"screen looks blank: {len(colours)} colours")
    check(len(set(rows[:18])) > 1, "header band is empty")
    after = d.state()
    check((after["screen"], after["title"], after["depth"]) ==
          (before["screen"], before["title"], before["depth"]), f"shot changed the screen: {after}")


def t28_keyboard_matrix(d):
    """Keys held on the keyboard matrix (the `keys` hook) go through the
    library's own key list, key state and keymap, then our key mapping,
    rolls and auto-repeat. Not covered: the I2C read and the switches."""
    pw = P3 if SD_CARD else P1  # see t22
    if d.state()["screen"] == "unlock":
        d.unlock(pw)
    go_home(d)
    title = f"Keys{int(time.time()) % 100000}"  # unique, so reruns find only this run's
    new_entry(d, "Note")
    set_title(d, title)
    d.key("down")
    d.key("enter")
    d.screen("text_input", "Text")

    def length():
        return int(info(d.state())["len"])

    expect = ""

    # Every printable key, unshifted and then shifted together with Shift.
    for name, plain, _ in PRINTABLE:
        d.tap(name)
        expect += plain
    for name, _, shifted in PRINTABLE:
        d.tap(name, "shift")
        expect += shifted
    check(length() == len(expect), f"typed {length()} characters, expected {len(expect)}")

    # Modifiers and Opt alone type nothing.
    for name in ("fn", "shift", "ctrl", "alt", "opt"):
        d.tap(name)
    check(length() == len(expect), f"a modifier alone typed a character: {length()}")

    # Enter and Backspace from the matrix.
    d.tap("enter")
    d.tap("a")
    d.tap("b")
    d.tap("bs")
    expect += "\na"
    check(length() == len(expect), f"enter/backspace: {length()} vs {len(expect)}")

    # Key rolls: the next key goes down before the last one is released.
    for roll in (("a", "b"), ("q", "w", "e")):
        d.roll(*roll)
        expect += "".join(roll)
    check(length() == len(expect), f"rolled keys: {length()} vs {len(expect)}")

    # fn + ; . , /  move the cursor (Up Down Left Right).
    d.tap("x")
    d.tap(",", "fn")  # Left
    d.tap("y")
    d.tap("/", "fn")  # Right
    d.tap("z")
    expect += "yxz"
    d.tap("enter")
    d.tap("a")
    d.tap("b")
    d.tap("enter")
    d.tap("c")
    d.tap("d")
    d.tap(";", "fn")  # Up: end of "ab"
    d.tap("1")
    d.tap(".", "fn")  # Down: end of "cd"
    d.tap("2")
    expect += "\nab1\ncd2"
    check(length() == len(expect), f"fn arrows: {length()} vs {len(expect)}")

    # Held keys repeat after 450 ms, then every 70 ms: 1 + (1200 - 450) / 70 = 11.
    before = length()
    d.hold(1200, "x")
    time.sleep(0.2)
    n = length() - before
    check(10 <= n <= 12, f"held x typed {n} characters in 1.2 s, expected 11")
    d.key("bs", n)
    # Enter does not repeat.
    before = length()
    d.hold(1000, "enter")
    time.sleep(0.2)
    check(length() - before == 1, f"held enter typed {length() - before} newlines")
    d.key("bs")
    check(length() == len(expect), f"after repeat checks: {length()} vs {len(expect)}")

    # fn + ` is Esc: asks before dropping the text; declining keeps it.
    d.tap("`", "fn")
    d.screen("confirm", "Discard changes?")
    d.key("n")
    st = d.screen("text_input", "Text")
    check(int(info(st)["len"]) == len(expect), f"declining lost text: {st}")

    # Tab finishes the editor, then the note is stored exactly as typed.
    d.tap("tab")
    d.screen("entry_edit")
    save_entry(d)
    d.screen("entry_view")
    notes = [e for e in d.dump() if e["title"] == title]
    check(len(notes) == 1, f"note not saved: {notes}")
    got = notes[0]["fields"]["1"]
    check(got == expect, f"typed text differs:\n  got      {got!r}\n  expected {expect!r}")

    # Hold Space to reveal (the held-key path: isKeyPressed on the real key list).
    check(info(d.state())["held"] == "0", "space must start released")
    d.keys("space")
    d.wait(lambda s: info(s).get("held") == "1", "hold-to-reveal with a held matrix key")
    d.keys()
    d.wait(lambda s: info(s).get("held") == "0", "release hides again")

    # Opt toggles reveal in a masked field, once per press, not while held.
    new_entry(d, "Password")
    d.key("down", 2)
    d.key("enter")
    st = d.screen("text_input", "Password")
    check(info(st)["reveal"] == "0", f"masked field must start hidden: {st}")
    d.tap("opt")
    d.wait(lambda s: info(s)["reveal"] == "1", "opt shows")
    d.tap("opt")
    d.wait(lambda s: info(s)["reveal"] == "0", "opt hides")
    d.hold(1000, "opt")
    d.wait(lambda s: info(s)["reveal"] == "1", "a held opt toggles once")
    d.tap("`", "fn")  # nothing typed: closes without asking
    d.screen("entry_edit")
    go_home(d)

    # Leave the vault as this scenario found it.
    delete_entry(d, title)
    check(not any(e["title"] == title for e in d.dump()), "test note left behind")


SD_FAIL_TITLES = ("Restore", "Backup failed", "SD card not readable")


def wait_message(d, titles, timeout=60):
    """Wait for a message screen with one of `titles` while the device may be
    stuck in a long SD call (state requests then go unanswered for a while).
    Returns (state, seconds waited)."""
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        try:
            last = d.state()
        except Fail:
            continue  # busy inside the SD driver; its answer comes later
        if last["screen"] == "message" and last["title"] in titles:
            return last, time.time() - t0
        time.sleep(0.2)
    raise Fail(f"no {titles} message within {timeout} s; last state: {last}")


def backup_now(d):
    """Settings > Backup to SD with the P3 passphrase; returns the file name."""
    open_setting(d, ROW_BACKUP)
    d.screen("text_input", "Backup passphrase", timeout=15)
    d.type(P3)
    d.key("enter")
    d.screen("text_input", "Confirm passphrase")
    d.type(P3)
    d.key("enter")
    st = d.screen("message", "Backup saved", timeout=60)
    name = backup_name_in(st)
    d.key("enter")
    return name


def restore_menu_names(d):
    """Open Settings > Restore from SD and return the backup names, newest first."""
    open_setting(d, ROW_RESTORE)
    st = d.screen("menu", "Choose backup", timeout=15)
    return info(st)["items"].split("|")


def t29_sd_pulled(d):
    """Pulling the card during a backup or restore ends in an error message,
    never a freeze, and G0 locks afterwards. The pull is simulated by holding
    the SPI MISO input high (`sdcut`): the real SD driver and FAT code fail
    the way they do with no card. G0 is not serviced while the main loop is
    inside a blocking SD call, so it is pressed once the message is up."""
    need_card()
    pw = P3

    def g0_locks():
        d.cmd("g0")
        d.screen("unlock")
        d.unlock(pw)

    def finish_failed(titles):
        """After a pull: the message is shown, the vault is untouched, G0 locks."""
        st, waited = wait_message(d, titles)
        d.cmd("sdcut off")
        check(d.cmd("ping") == "pong", "device does not answer after the pull")
        check(st["fails"] == 0, f"a pulled card must not count an attempt: {st}")
        d.key("enter")
        g0_locks()
        return waited

    d.cmd("sdcut off")
    try:
        if d.state()["screen"] == "unlock":
            d.unlock(pw)
        before = d.dump()
        backup_now(d)  # a good backup, so the card is never empty ("No backups")
        known = set(restore_menu_names(d))  # backups on the card before any pull
        go_home(d)

        # Restore: card pulled with the list on screen, before a file is read.
        restore_menu_names(d)
        d.cmd("sdcut on")
        d.key("enter")
        finish_failed(("Restore", "SD card not readable"))

        # Backup: card pulled while the passphrase is asked for.
        open_setting(d, ROW_BACKUP)
        d.screen("text_input", "Backup passphrase", timeout=15)
        d.cmd("sdcut on")
        d.type(P3)
        d.key("enter")
        d.screen("text_input", "Confirm passphrase")
        d.type(P3)
        d.key("enter")
        finish_failed(("Backup failed", "SD card not readable"))

        # Backup: card pulled N ms into the write (lands in the listing, the
        # file write or the read-back check, depending on N).
        outcomes = {}
        for ms in (0, 1, 2, 5, 10, 20, 50, 100, 250, 500):
            open_setting(d, ROW_BACKUP)
            d.screen("text_input", "Backup passphrase", timeout=15)
            d.cmd(f"sdcut {ms}")
            d.type(P3)
            d.key("enter")
            d.screen("text_input", "Confirm passphrase")
            d.type(P3)
            d.key("enter")
            st, waited = wait_message(d, ("Backup saved",) + SD_FAIL_TITLES)
            outcomes[ms] = (st["title"], round(waited, 1))
            check(waited < 40, f"cut after {ms} ms: {waited:.0f} s to report: {st}")
            d.cmd("sdcut off")
            check(st["fails"] == 0, f"cut after {ms} ms counted an attempt: {st}")
            d.key("enter")
            g0_locks()
        print(f"  cut after N ms -> (message, seconds): {outcomes}", flush=True)
        check(any(t != "Backup saved" for t, _ in outcomes.values()),
              f"no cut ever made a backup fail: {outcomes}")

        # Whatever the cuts left on the card: a file that is not a complete
        # backup is refused, and nothing replaces the vault.
        d.cmd("sdcut off")
        names = restore_menu_names(d)
        left = [n for n in names if n not in known]
        go_home(d)
        for name in left:
            names = restore_menu_names(d)
            d.key("down", names.index(name))
            d.key("enter")
            st = d.state()
            if st["screen"] == "message":  # unreadable: refused before the prompt
                check(st["title"] in SD_FAIL_TITLES, f"{name}: {st}")
                d.key("enter")
                continue
            d.screen("text_input", "Backup passphrase")
            d.type(P3)
            d.key("enter")
            st = d.wait(lambda s: s["screen"] in ("confirm", "message"), f"result for {name}", 30)
            if st["screen"] == "confirm":
                check(st["title"] == "Replace vault?", f"{name}: {st}")
                d.key("n")  # a complete backup: fine, keep the vault
            else:
                check(st["title"] == "Restore failed", f"{name}: {st}")
                d.key("enter")
            check(st["fails"] == 0, f"{name}: restore guess counted: {st}")
        go_home(d)
        check(d.dump() == before, "a pulled card changed the vault")

        # And the card still works: backup, change the vault, restore it.
        good = backup_now(d)
        new_entry(d, "Note")
        set_title(d, "After pulls")
        d.key("down")
        d.key("enter")
        d.screen("text_input", "Text")
        d.type("temp")
        d.key("tab")
        d.screen("entry_edit")
        save_entry(d)
        d.screen("entry_view")
        check(len(d.dump()) == len(before) + 1, "note not added")
        names = restore_menu_names(d)
        check(names[0] == good, f"newest backup not first: {names}")
        d.key("enter")
        d.screen("text_input", "Backup passphrase")
        d.type(P3)
        d.key("enter")
        d.screen("confirm", "Replace vault?", timeout=30)
        d.key("y")
        d.screen("confirm", "Restored", timeout=30)
        d.key("n")
        d.screen("home")
        check(d.dump() == before, "restore after the pulls did not bring back the vault")
    finally:
        try:
            d.cmd("sdcut off")
        except Fail:
            pass


SCENARIOS = [v for k, v in sorted(globals().items()) if re.match(r"t\d\d_", k)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("-k", default="", help="only run scenarios whose name contains this")
    ap.add_argument("--sd", action="store_true", help="a FAT32 SD card is inserted")
    args = ap.parse_args()
    global SD_CARD
    SD_CARD = args.sd
    d = Device(args.port)
    passed = failed = skipped = 0
    for fn in SCENARIOS:
        if args.k and args.k not in fn.__name__:
            continue
        t0 = time.time()
        try:
            try:
                fn(d)
            finally:
                # A scenario that stopped half way must not leave a simulated
                # key held for the next one.
                try:
                    d.cmd("keys", timeout=5)
                except Exception:
                    pass
            passed += 1
            print(f"PASS {fn.__name__} ({time.time() - t0:.1f}s)", flush=True)
        except Skip as e:
            skipped += 1
            print(f"SKIP {fn.__name__}: {e}", flush=True)
        except Exception as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {e}", flush=True)
            if not isinstance(e, Fail):
                traceback.print_exc()
            if not args.k:
                print("stopping: later scenarios depend on earlier ones")
                break
    print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
    if d.stale or d.unreadable:
        print(f"transport: {d.stale} late replies skipped, "
              f"{len(d.unreadable)} requests the device could not parse")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
