#!/usr/bin/env python3
"""End-to-end tests: drive CardVault's real UI on a Cardputer over USB.

  pio run -e cardputer-e2e -t upload
  python3 tools/e2e/run.py /dev/cu.usbmodem1101 [-k name_filter] [--sd]

Without --sd the device must have NO SD card: the SD scenarios then check
the "No SD card" paths. With --sd a FAT32 card must be inserted: backup,
restore (also from the setup screen, #43) and Compact run against it, and
leave backup-*.cvb files in /cardvault on the card.

The e2e firmware keeps its vault at 0x410000 and uses separate NVS
namespaces, so a real vault on the device is never touched. Requires
pyserial (PlatformIO's Python has it).
"""
import argparse
import hashlib
import hmac
import json
import re
import struct
import sys
import time
import traceback

import serial

P1 = "correct horse 42"
P2 = "battery staple 77"
P3 = "backup phrase 99"
SD_CARD = False  # set by --sd
TOTP_SECRET = "JBSWY3DPEHPK3PXP"


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
        self.connect()

    # -- transport ---------------------------------------------------------------

    def connect(self, timeout=25):
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.ser = serial.Serial(self.port, 115200, timeout=0.2)
                time.sleep(0.3)
                self.ser.reset_input_buffer()
                self.rx = b""
                if self.cmd("ping", timeout=2) == "pong":
                    return
            except (serial.SerialException, OSError, Fail):
                pass
            try:
                if self.ser:
                    self.ser.close()
            except Exception:
                pass
            time.sleep(0.5)
        raise Fail(f"device on {self.port} not responding")

    def cmd(self, line, timeout=10):
        """Send `line` with a fresh request id and return the payload of the
        reply that carries the same id. Replies to earlier requests (late
        answers after a timeout) are counted and skipped. A dropped link is
        not retried: on native USB it means the device reset, and resending
        would hide that."""
        self.seq += 1
        rid = str(self.seq)
        self.ser.write(f"{rid} {line}\n".encode())
        end = time.time() + timeout
        while time.time() < end:
            self.rx += self.ser.read(256)
            while b"\n" in self.rx:
                raw, self.rx = self.rx.split(b"\n", 1)
                m = REPLY.match(raw.decode(errors="replace").strip())
                if not m:
                    continue  # boot output, CVTEST READY
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
        try:
            self.ser.close()
        except Exception:
            pass
        self.connect()

    def hard_reset(self):
        """Pulse EN via RTS (like pulling power) and reconnect."""
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        time.sleep(1.5)
        try:
            self.ser.close()
        except Exception:
            pass
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

    def unlock(self, pw):
        self.screen("unlock")
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


def delete_entry(d, title):
    go_home(d)
    d.type(title)
    d.wait(lambda s: info(s)["rows"] == "1", f"search finds {title}")
    d.key("enter")
    d.screen("entry_view")
    d.key("tab")
    d.menu_pick("Delete")
    d.screen("confirm", "Delete secret?")
    d.key("y")
    d.screen("home")
    d.key("esc")  # clear the search
    d.screen("home")


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
    try:
        d.ser.close()
    except Exception:
        pass
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
    d.cmd("press y 1")
    try:
        st = d.screen("home")
        time.sleep(1.5)  # > 450 ms repeat delay
    finally:
        d.cmd("press y 0")
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
            fn(d)
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
