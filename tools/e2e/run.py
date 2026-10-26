#!/usr/bin/env python3
"""End-to-end tests: drive CardVault's real UI on a Cardputer over USB.

  pio run -e cardputer-e2e -t upload
  python3 tools/e2e/run.py /dev/cu.usbmodem1101 [-k name_filter]

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
TOTP_SECRET = "JBSWY3DPEHPK3PXP"


class Fail(Exception):
    pass


class Device:
    def __init__(self, port):
        self.port = port
        self.ser = None
        self.connect()

    # -- transport ---------------------------------------------------------------

    def connect(self, timeout=25):
        end = time.time() + timeout
        while time.time() < end:
            try:
                self.ser = serial.Serial(self.port, 115200, timeout=0.2)
                time.sleep(0.3)
                self.ser.reset_input_buffer()
                if self.cmd("ping", timeout=2, retry=False) == "pong":
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

    def cmd(self, line, timeout=10, retry=True):
        try:
            self.ser.write((line + "\n").encode())
            end = time.time() + timeout
            buf = b""
            while time.time() < end:
                buf += self.ser.read(256)
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    text = raw.decode(errors="replace").strip()
                    if text.startswith("OK"):
                        return text[3:]
                    if text.startswith("ERR"):
                        raise Fail(f"{line!r}: {text}")
        except (serial.SerialException, OSError):
            if not retry:
                raise
            self.connect()
            return self.cmd(line, timeout, retry=False)
        raise Fail(f"no reply to {line!r}")

    def restart_cmd(self, line):
        """Send a command that reboots the device, then reconnect."""
        try:
            self.ser.write((line + "\n").encode())
            self.ser.flush()
        except serial.SerialException:
            pass
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
        return json.loads(self.cmd("dump"))

    def key(self, name, times=1):
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
    d.screen("message", "Passphrase changed", timeout=40)
    d.key("enter")
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
    check(d.dump() == [], "data left after wipe")


def t17_erase_from_settings(d):
    t02_setup_validation_and_create(d)
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
    check(d.dump() == [] and st["fails"] == 0, f"vault not erased: {st}")


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


SCENARIOS = [v for k, v in sorted(globals().items()) if re.match(r"t\d\d_", k)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("-k", default="", help="only run scenarios whose name contains this")
    args = ap.parse_args()
    d = Device(args.port)
    passed = failed = 0
    for fn in SCENARIOS:
        if args.k and args.k not in fn.__name__:
            continue
        t0 = time.time()
        try:
            fn(d)
            passed += 1
            print(f"PASS {fn.__name__} ({time.time() - t0:.1f}s)", flush=True)
        except Exception as e:
            failed += 1
            print(f"FAIL {fn.__name__}: {e}", flush=True)
            if not isinstance(e, Fail):
                traceback.print_exc()
            if not args.k:
                print("stopping: later scenarios depend on earlier ones")
                break
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
