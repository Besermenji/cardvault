#!/usr/bin/env python3
"""Check (or regenerate) the donation addresses and their QR codes.

docs/support/addresses.env is the only source of the addresses. Every copy (README, M5Burner text)
must match it exactly, the web flasher gets them filled in at release time, and no other BTC/ETH
address may appear in the repo, so a swapped, mistyped or stale address fails CI and the release.

Usage: python3 tools/support.py          check (CI and release)
       python3 tools/support.py --write  regenerate docs/flasher/{btc,eth}.svg
Needs: pip install -r tools/support-requirements.txt (pinned: the SVGs must match segno byte for byte)
"""
import hashlib
import io
import pathlib
import re
import subprocess
import sys

import resvg_py
import segno
import zxingcpp
from Crypto.Hash import keccak
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
ENV_FILE = ROOT / "docs/support/addresses.env"
QR_DIR = ROOT / "docs/flasher"
COPIES = ["README.md", "docs/PUBLISHING.md"]
TEMPLATE = "docs/flasher/index.html"  # __BTC__ / __ETH__, filled in by release.yml
FUNDING_LINK = "https://github.com/Besermenji/cardvault#support"
ENV_LINE = re.compile(r"(BTC|ETH)=([0-9A-Za-z]+)")

# Self-test vectors, allowed in this file only.
BIP173_VECTOR = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"
EIP55_VECTOR = "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"
P2PKH_VECTOR = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa"
P2SH_VECTOR = "3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy"
TEST_VECTORS = {BIP173_VECTOR, EIP55_VECTOR, EIP55_VECTOR.lower(), P2PKH_VECTOR, P2SH_VECTOR}


def fail(errors):
    for e in errors:
        print("FAIL", e)
    sys.exit(1)


def load_addresses():
    found, errors = {}, []
    for n, line in enumerate(ENV_FILE.read_text().splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        m = ENV_LINE.fullmatch(line)
        if not m or m[1] in found:
            errors.append(f"addresses.env:{n}: expected one BTC=<address> and one ETH=<address> line "
                          "(no quotes, spaces, export or other keys)")
        else:
            found[m[1]] = m[2]
    errors += [f"addresses.env: {key} missing" for key in ("BTC", "ETH") if key not in found]
    if errors:
        fail(errors)
    return found["BTC"], found["ETH"]


# --- BTC: segwit addresses, bech32 / bech32m (BIP173, BIP350) ---

BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
BECH32_CONST, BECH32M_CONST = 1, 0x2BC830A3


def _polymod(values):
    gen = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    chk = 1
    for v in values:
        top = chk >> 25
        chk = (chk & 0x1FFFFFF) << 5 ^ v
        for i in range(5):
            chk ^= gen[i] if (top >> i) & 1 else 0
    return chk


def _hrp_expand(hrp):
    return [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp]


def _convertbits(data, frombits, tobits, pad):
    acc = bits = 0
    out = []
    for v in data:
        acc = (acc << frombits) | v
        bits += frombits
        while bits >= tobits:
            bits -= tobits
            out.append((acc >> bits) & ((1 << tobits) - 1))
    if pad and bits:
        out.append((acc << (tobits - bits)) & ((1 << tobits) - 1))
    elif not pad and (bits >= frombits or (acc << (tobits - bits)) & ((1 << tobits) - 1)):
        return None  # leftover bits must be fewer than 5 and all zero
    return out


def _checksummed(hrp, values, const):
    chk = _polymod(_hrp_expand(hrp) + values + [0] * 6) ^ const
    values = values + [(chk >> 5 * (5 - i)) & 31 for i in range(6)]
    return hrp + "1" + "".join(BECH32_CHARSET[v] for v in values)


def segwit_encode(hrp, version, program):
    """Only used by the self-test to build structurally invalid addresses with valid checksums."""
    const = BECH32_CONST if version == 0 else BECH32M_CONST
    return _checksummed(hrp, [version] + _convertbits(program, 8, 5, True), const)


def segwit_problem(addr, hrp="bc"):
    """None if addr is a valid segwit address for hrp, else what is wrong (BIP173/BIP350 rules)."""
    if len(addr) > 90:
        return "longer than 90 characters"
    if addr != addr.lower():
        return "must be lowercase"
    if not addr.startswith(hrp + "1"):
        return f"must start with {hrp}1"
    data = addr[len(hrp) + 1:]
    if len(data) < 7 or any(c not in BECH32_CHARSET for c in data):
        return "invalid bech32 characters"
    values = [BECH32_CHARSET.index(c) for c in data]
    version = values[0]
    if version > 16:
        return f"witness version {version} > 16"
    const = BECH32_CONST if version == 0 else BECH32M_CONST
    if _polymod(_hrp_expand(hrp) + values) != const:
        return "checksum mismatch (typo?)"
    program = _convertbits(values[1:-6], 5, 8, False)
    if program is None:
        return "non-zero padding bits"
    if not 2 <= len(program) <= 40:
        return f"witness program is {len(program)} bytes, must be 2-40"
    if version == 0 and len(program) not in (20, 32):
        return f"v0 witness program is {len(program)} bytes, must be 20 or 32"
    if version == 1 and len(program) != 32:
        return f"v1 (taproot) witness program is {len(program)} bytes, must be 32"
    return None


# --- BTC: legacy base58check addresses (only scanned for, never used) ---

BASE58_CHARSET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def is_base58_address(s):
    n = 0
    for c in s:
        n = n * 58 + BASE58_CHARSET.index(c)
    raw = b"\x00" * (len(s) - len(s.lstrip("1"))) + n.to_bytes((n.bit_length() + 7) // 8, "big")
    if len(raw) != 25 or raw[0] not in (0x00, 0x05):  # P2PKH / P2SH mainnet
        return False
    return hashlib.sha256(hashlib.sha256(raw[:21]).digest()).digest()[:4] == raw[21:]


# --- ETH: EIP-55 mixed-case checksum ---

def eip55(addr):
    hexpart = addr[2:].lower()
    h = keccak.new(digest_bits=256, data=hexpart.encode()).hexdigest()
    return "0x" + "".join(c.upper() if c.isalpha() and int(h[i], 16) >= 8 else c
                          for i, c in enumerate(hexpart))


def eth_problem(addr):
    if not re.fullmatch(r"0x[0-9a-fA-F]{40}", addr):
        return "must be 0x + 40 hex digits"
    if addr != eip55(addr):
        return f"EIP-55 checksum mismatch (typo?), expected casing {eip55(addr)}"
    return None


def self_test():
    """Runs before anything is read or written; explicit checks, so `python -O` can't skip them."""
    # A 32-byte program leaves 4 spare bits in the last group; set one and re-checksum.
    padded = [BECH32_CHARSET.index(c) for c in segwit_encode("bc", 0, bytes(32))[3:-6]]
    padded[-1] |= 1
    cases = [
        (segwit_problem(BIP173_VECTOR), None),
        (segwit_problem(segwit_encode("bc", 1, bytes(32))), None),
        (segwit_problem(BIP173_VECTOR[:-1] + "5"), "checksum mismatch (typo?)"),
        (segwit_problem(segwit_encode("bc", 0, bytes(21))), "v0 witness program is 21 bytes, must be 20 or 32"),
        (segwit_problem(segwit_encode("bc", 1, bytes(31))), "v1 (taproot) witness program is 31 bytes, must be 32"),
        (segwit_problem(segwit_encode("bc", 2, bytes(41))), "witness program is 41 bytes, must be 2-40"),
        (segwit_problem(segwit_encode("bc", 17, bytes(32))), "witness version 17 > 16"),
        (segwit_problem(segwit_encode("bc", 2, bytes(40)) + "q" * 20), "longer than 90 characters"),
        (segwit_problem(_checksummed("bc", padded, BECH32_CONST)), "non-zero padding bits"),
        (eip55(EIP55_VECTOR.lower()), EIP55_VECTOR),
        (eth_problem(EIP55_VECTOR), None),
        (is_base58_address(P2PKH_VECTOR), True),
        (is_base58_address(P2SH_VECTOR), True),
        (is_base58_address(P2SH_VECTOR[:-1] + "z"), False),
    ]
    bad = [f"self-test case {i} failed: got {got!r}, expected {want!r}"
           for i, (got, want) in enumerate(cases) if got != want]
    if bad:
        fail(bad)


# --- QR codes ---

def qr_payloads(btc, eth):
    return {"btc.svg": f"bitcoin:{btc}", "eth.svg": eth}


def svg_bytes(payload):
    out = io.BytesIO()
    segno.make(payload, error="m").save(out, kind="svg", scale=6, border=4, dark="#000",
                                        light="#fff", xmldecl=False)
    return out.getvalue()


def decode_svg(data):
    """Render the SVG file with resvg and decode it with zxing-cpp: both independent of segno."""
    png = bytes(resvg_py.svg_to_bytes(svg_string=data.decode()))
    return [r.text for r in zxingcpp.read_barcodes(Image.open(io.BytesIO(png)).convert("L"))]


# --- repo scan ---

def is_token(text, addr):
    return re.search(rf"(?<![0-9A-Za-z]){re.escape(addr)}(?![0-9A-Za-z])", text) is not None


def foreign_addresses(text, btc, eth):
    """Every BTC/ETH address in text other than ours, plus any corrupted copy of ours."""
    found = []
    # No left word boundary: an address glued to a preceding word must not slip through.
    for m in re.finditer(r"(?:bc|tb|bcrt)1[02-9ac-hj-np-z]{6,}", text, re.I):
        s = m.group()
        if s != btc and (btc in s or not segwit_problem(s.lower(), s.lower().split("1")[0])):
            found.append((m.start(), s))
    for m in re.finditer(r"0x[0-9a-fA-F]{40,}", text):
        if m.group() != eth:
            found.append((m.start(), m.group()))
    for m in re.finditer(rf"[{BASE58_CHARSET}]{{26,}}", text):
        run = m.group()
        for i, c in enumerate(run):
            if c in "13":
                for n in range(26, min(35, len(run) - i) + 1):
                    if is_base58_address(run[i:i + n]):
                        found.append((m.start() + i, run[i:i + n]))
    return found


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode().split("\0") if p]


def main():
    self_test()
    btc, eth = load_addresses()
    payloads = qr_payloads(btc, eth)
    errors = [f"addresses.env {name}: {problem(addr)}"
              for name, addr, problem in (("BTC", btc, segwit_problem), ("ETH", eth, eth_problem))
              if problem(addr)]
    if errors:
        fail(errors)

    if "--write" in sys.argv:
        for name, payload in payloads.items():
            (QR_DIR / name).write_bytes(svg_bytes(payload))
        print("wrote", ", ".join(payloads))

    for name, payload in payloads.items():
        data = (QR_DIR / name).read_bytes()
        if data != svg_bytes(payload):
            errors.append(f"docs/flasher/{name} is not segno's QR of {payload!r} (run with --write)")
        decoded = decode_svg(data)
        if decoded != [payload]:
            errors.append(f"docs/flasher/{name} scans as {decoded}, expected {payload!r}")

    for rel in COPIES:
        text = (ROOT / rel).read_text()
        for name, addr in (("BTC", btc), ("ETH", eth)):
            if not is_token(text, addr):
                errors.append(f"{rel}: {name} address missing or different")
    template = (ROOT / TEMPLATE).read_text()
    for placeholder in ("__BTC__", "__ETH__"):
        if placeholder not in template:
            errors.append(f"{TEMPLATE}: {placeholder} placeholder missing")

    for rel in tracked_files():
        path = ROOT / rel
        if not path.is_file():
            continue
        try:
            text = path.read_text()
        except UnicodeDecodeError:
            continue  # binary
        for pos, addr in foreign_addresses(text, btc, eth):
            if rel == "tools/support.py" and addr in TEST_VECTORS:
                continue
            errors.append(f"{rel}:{text.count(chr(10), 0, pos) + 1}: unknown address {addr}")

    if FUNDING_LINK not in (ROOT / ".github/FUNDING.yml").read_text():
        errors.append(f".github/FUNDING.yml must link {FUNDING_LINK}")

    if errors:
        fail(errors)
    print(f"OK: BTC {btc}, ETH {eth}; QR codes, {len(COPIES)} copies and the flasher template match")


if __name__ == "__main__":
    main()
