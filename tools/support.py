#!/usr/bin/env python3
"""Check (or regenerate) the donation addresses and their QR codes.

docs/support/addresses.env is the only source of the addresses. Every copy (README, web flasher,
M5Burner text) must match it exactly, and no other address may appear in the repo, so a swapped,
mistyped or stale address fails CI.

Usage: python3 tools/support.py          check (CI)
       python3 tools/support.py --write  regenerate docs/support/{btc,eth}.svg
Needs: pip install segno zxing-cpp numpy
"""
import io
import pathlib
import re
import subprocess
import sys

import numpy
import segno
import zxingcpp

ROOT = pathlib.Path(__file__).resolve().parent.parent
SUPPORT = ROOT / "docs/support"
COPIES = ["README.md", "docs/flasher/index.html", "docs/PUBLISHING.md"]
FUNDING_LINK = "https://github.com/Besermenji/cardvault#support"
ADDRESS_RE = re.compile(r"\b(?:bc1|tb1|bcrt1)[02-9ac-hj-np-z]{11,71}\b|\b0x[0-9a-f]{40}\b", re.I)


def load_addresses():
    out = {}
    for line in (SUPPORT / "addresses.env").read_text().splitlines():
        if line and not line.startswith("#"):
            key, value = line.split("=", 1)
            out[key] = value
    return out["BTC"], out["ETH"]


# --- BTC: bech32 / bech32m (BIP173, BIP350) ---

BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"


def _polymod(values):
    gen = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    chk = 1
    for v in values:
        top = chk >> 25
        chk = (chk & 0x1FFFFFF) << 5 ^ v
        for i in range(5):
            chk ^= gen[i] if (top >> i) & 1 else 0
    return chk


def check_btc(addr):
    if addr != addr.lower() or not addr.startswith("bc1"):
        return "must be a lowercase mainnet bech32 address (bc1...)"
    hrp, data = addr[:2], addr[3:]
    if any(c not in BECH32_CHARSET for c in data) or len(data) < 7:
        return "invalid bech32 characters"
    values = [BECH32_CHARSET.index(c) for c in data]
    expanded = [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp]
    version = values[0]
    want = 1 if version == 0 else 0x2BC830A3  # bech32 for v0, bech32m for v1+
    if _polymod(expanded + values) != want:
        return "bech32 checksum mismatch (typo?)"
    bits = len(values[1:-6]) * 5 // 8
    if version == 0 and bits not in (20, 32):
        return f"v0 witness program must be 20 or 32 bytes, got {bits}"
    return None


# --- ETH: EIP-55 mixed-case checksum (needs Keccak-256, which hashlib lacks) ---

MASK = (1 << 64) - 1
ROT = [[0, 36, 3, 41, 18], [1, 44, 10, 45, 2], [62, 6, 43, 15, 61],
       [28, 55, 25, 21, 56], [27, 20, 39, 8, 14]]


def _rol(v, n):
    return ((v << n) | (v >> (64 - n))) & MASK


def _keccak_f(a):
    r = 1
    for _ in range(24):
        c = [a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20] for x in range(5)]
        d = [c[(x - 1) % 5] ^ _rol(c[(x + 1) % 5], 1) for x in range(5)]
        a = [a[i] ^ d[i % 5] for i in range(25)]
        b = [0] * 25
        for x in range(5):
            for y in range(5):
                b[y + 5 * ((2 * x + 3 * y) % 5)] = _rol(a[x + 5 * y], ROT[x][y])
        a = [b[i] ^ (~b[(i + 1) % 5 + i // 5 * 5] & b[(i + 2) % 5 + i // 5 * 5]) for i in range(25)]
        for j in range(7):
            r = ((r << 1) ^ ((r >> 7) * 0x71)) % 256
            if r & 2:
                a[0] ^= 1 << ((1 << j) - 1)
    return a


def keccak256(data):
    rate = 136
    msg = bytearray(data) + b"\x01"
    msg += b"\x00" * (-len(msg) % rate)
    msg[-1] |= 0x80
    a = [0] * 25
    for off in range(0, len(msg), rate):
        for i in range(rate // 8):
            a[i] ^= int.from_bytes(msg[off + 8 * i:off + 8 * i + 8], "little")
        a = _keccak_f(a)
    return b"".join(a[i].to_bytes(8, "little") for i in range(4))


def eip55(addr):
    hexpart = addr[2:].lower()
    h = keccak256(hexpart.encode()).hex()
    return "0x" + "".join(c.upper() if c.isalpha() and int(h[i], 16) >= 8 else c
                          for i, c in enumerate(hexpart))


def check_eth(addr):
    if not re.fullmatch(r"0x[0-9a-fA-F]{40}", addr):
        return "must be 0x + 40 hex digits"
    if addr != eip55(addr):
        return f"EIP-55 checksum mismatch (typo?), expected casing {eip55(addr)}"
    return None


# --- QR codes ---

def qr_payloads(btc, eth):
    return {"btc.svg": f"bitcoin:{btc}", "eth.svg": eth}


def make_qr(payload):
    return segno.make(payload, error="m")


def svg_bytes(qr):
    out = io.BytesIO()
    qr.save(out, kind="svg", scale=6, border=4, dark="#000", light="#fff", xmldecl=False)
    return out.getvalue()


def decode_qr(qr):
    """Decode the QR matrix with zxing-cpp, a decoder independent of segno."""
    modules = numpy.array(qr.matrix, dtype=numpy.uint8)
    image = numpy.where(modules == 1, 0, 255).astype(numpy.uint8)
    image = numpy.pad(image.repeat(4, 0).repeat(4, 1), 16, constant_values=255)
    found = zxingcpp.read_barcodes(image)
    return [r.text for r in found]


def main():
    btc, eth = load_addresses()
    payloads = qr_payloads(btc, eth)

    if "--write" in sys.argv:
        for name, payload in payloads.items():
            (SUPPORT / name).write_bytes(svg_bytes(make_qr(payload)))
        print("wrote", ", ".join(payloads))

    assert keccak256(b"").hex() == "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"
    assert eip55("0x5aaeb6053f3e94c9b9a09f33669435e7ef1beaed") == "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed"

    errors = []
    for name, addr, check in (("BTC", btc, check_btc), ("ETH", eth, check_eth)):
        problem = check(addr)
        if problem:
            errors.append(f"addresses.env {name}: {problem}")

    for name, payload in payloads.items():
        qr = make_qr(payload)
        if (SUPPORT / name).read_bytes() != svg_bytes(qr):
            errors.append(f"docs/support/{name} is not the QR of {payload!r} (run with --write)")
        decoded = decode_qr(qr)
        if decoded != [payload]:
            errors.append(f"docs/support/{name} decodes to {decoded}, expected {payload!r}")

    for rel in COPIES:
        text = (ROOT / rel).read_text()
        for name, addr in (("BTC", btc), ("ETH", eth)):
            if addr not in text:
                errors.append(f"{rel}: {name} address missing or different")

    tracked = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True,
                             check=True).stdout.split()
    for rel in tracked:
        if rel == "tools/support.py":  # holds the EIP-55 test vector, not a donation address
            continue
        try:
            text = (ROOT / rel).read_text()
        except (UnicodeDecodeError, FileNotFoundError):
            continue
        for match in ADDRESS_RE.finditer(text):
            if match.group() not in (btc, eth):
                line = text.count("\n", 0, match.start()) + 1
                errors.append(f"{rel}:{line}: unknown address {match.group()}")

    if FUNDING_LINK not in (ROOT / ".github/FUNDING.yml").read_text():
        errors.append(f".github/FUNDING.yml must link {FUNDING_LINK}")

    for e in errors:
        print("FAIL", e)
    if errors:
        sys.exit(1)
    print(f"OK: BTC {btc}, ETH {eth}; QR codes and {len(COPIES)} copies match")


if __name__ == "__main__":
    main()
