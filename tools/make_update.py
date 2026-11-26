#!/usr/bin/env python3
"""Builds signed CardVault update files (.cvu) for "Update firmware" from the SD card (#85).

Layout (see src/core/update.h): 112 signed header bytes, a 64-byte Ed25519 signature over
them, then the app image (firmware.bin). Only the standard library; signing and checking use
OpenSSL 3 (`openssl pkeyutl -rawin`), so the release job installs nothing.

  make_update.py header   --image firmware.bin [--version X.Y.Z] --out signed.bin
  make_update.py assemble --header signed.bin --sig sig.bin --image firmware.bin \\
                          --pubkey pub.pem --out file.cvu
  make_update.py build    --image firmware.bin --key key.pem [--version X.Y.Z] --out file.cvu
  make_update.py check    --pubkey pub.pem file.cvu

`header` + `assemble` keep the private key out of this script (CI signs in between with
openssl); `build` does all three for local and e2e use. The version defaults to
src/version.h. Set CV_OPENSSL to pick the openssl binary.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGIC = b"CVUP"
FORMAT = 1
TARGET = "cardputer-adv"
SIGNED_SIZE = 112
HEADER_SIZE = SIGNED_SIZE + 64
SLOT_SIZE = 0x300000
VERSION_RE = re.compile(r"(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})\.(0|[1-9][0-9]{0,2})(-rc([1-9][0-9]{0,2}))?")


def fail(msg):
    sys.exit("make_update: " + msg)


def check_version(v):
    m = VERSION_RE.fullmatch(v)
    if not m or any(int(g) > 255 for g in m.group(1, 2, 3)) or (m.group(5) and int(m.group(5)) > 254):
        fail("version must be x.y.z or x.y.z-rcN (numbers up to 255, rc 1..254): %r" % v)
    return v


def source_version():
    text = open(os.path.join(ROOT, "src", "version.h")).read()
    m = re.search(r'#define CV_VERSION "([^"]+)"', text)
    if not m:
        fail("no CV_VERSION in src/version.h")
    return check_version(m.group(1))


def read_image(path):
    data = open(path, "rb").read()
    if not data or data[0] != 0xE9:
        fail("%s is not an ESP32 app image (first byte must be 0xE9)" % path)
    if len(data) > SLOT_SIZE:
        fail("%s is %d bytes, more than the 0x%x slot" % (path, len(data), SLOT_SIZE))
    return data


def fixed(s, n):
    b = s.encode("ascii")
    if not 0 < len(b) < n:
        fail("%r does not fit %d bytes" % (s, n))
    return b + b"\0" * (n - len(b))


def signed_header(version, image, target=TARGET):
    h = MAGIC + bytes([FORMAT, 0, 0, 0]) + fixed(version, 16) + fixed(target, 16)
    h += struct.pack("<II", len(image), 0) + hashlib.sha512(image).digest()
    assert len(h) == SIGNED_SIZE
    return h


def openssl():
    candidates = [os.environ.get("CV_OPENSSL"), "openssl", "/opt/homebrew/opt/openssl@3/bin/openssl",
                  "/usr/local/opt/openssl@3/bin/openssl"]
    for c in candidates:
        if not c or not shutil.which(c):
            continue
        out = subprocess.run([c, "version"], capture_output=True, text=True).stdout
        if out.startswith("OpenSSL 3"):
            return shutil.which(c)
    fail("needs OpenSSL 3 (LibreSSL can't sign raw Ed25519); set CV_OPENSSL")


def verify(signed, sig, pubkey):
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, "m"), "wb").write(signed)
        open(os.path.join(d, "s"), "wb").write(sig)
        r = subprocess.run([openssl(), "pkeyutl", "-verify", "-pubin", "-inkey", pubkey, "-rawin",
                            "-in", os.path.join(d, "m"), "-sigfile", os.path.join(d, "s")],
                           capture_output=True, text=True)
        return r.returncode == 0


def sign(signed, key):
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, "m"), "wb").write(signed)
        subprocess.run([openssl(), "pkeyutl", "-sign", "-inkey", key, "-rawin",
                        "-in", os.path.join(d, "m"), "-out", os.path.join(d, "s")], check=True)
        return open(os.path.join(d, "s"), "rb").read()


def public_from_private(key, out):
    subprocess.run([openssl(), "pkey", "-in", key, "-pubout", "-out", out], check=True)


def assemble(signed, sig, image, pubkey):
    if len(signed) != SIGNED_SIZE or signed[:4] != MAGIC:
        fail("not a header made by 'make_update.py header'")
    if len(sig) != 64:
        fail("signature must be 64 bytes, got %d" % len(sig))
    if signed != signed_header(signed[8:24].rstrip(b"\0").decode(), image,
                               signed[24:40].rstrip(b"\0").decode()):
        fail("header does not match this image")
    if not verify(signed, sig, pubkey):
        fail("signature does not verify with %s" % pubkey)
    return signed + sig + image


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("header")
    p.add_argument("--image", required=True)
    p.add_argument("--version")
    p.add_argument("--out", required=True)
    p = sub.add_parser("assemble")
    for a in ("--header", "--sig", "--image", "--pubkey", "--out"):
        p.add_argument(a, required=True)
    p = sub.add_parser("build")
    p.add_argument("--image", required=True)
    p.add_argument("--key", required=True)
    p.add_argument("--version")
    p.add_argument("--target", default=TARGET, help="tests only: a wrong target")
    p.add_argument("--out", required=True)
    p = sub.add_parser("check")
    p.add_argument("--pubkey", required=True)
    p.add_argument("file")
    args = ap.parse_args()

    if args.cmd == "header":
        version = check_version(args.version) if args.version else source_version()
        open(args.out, "wb").write(signed_header(version, read_image(args.image)))
    elif args.cmd == "assemble":
        image = read_image(args.image)
        data = assemble(open(args.header, "rb").read(), open(args.sig, "rb").read(), image, args.pubkey)
        open(args.out, "wb").write(data)
    elif args.cmd == "build":
        version = check_version(args.version) if args.version else source_version()
        image = read_image(args.image)
        signed = signed_header(version, image, args.target)
        with tempfile.TemporaryDirectory() as d:
            pub = os.path.join(d, "pub.pem")
            public_from_private(args.key, pub)
            data = assemble(signed, sign(signed, args.key), image, pub)
        open(args.out, "wb").write(data)
    elif args.cmd == "check":
        data = open(args.file, "rb").read()
        if len(data) < HEADER_SIZE or data[:4] != MAGIC:
            fail("%s is not a CardVault update file" % args.file)
        signed, sig, image = data[:SIGNED_SIZE], data[SIGNED_SIZE:HEADER_SIZE], data[HEADER_SIZE:]
        version = signed[8:24].rstrip(b"\0").decode("ascii", "replace")
        assemble(signed, sig, read_image_bytes(image), args.pubkey)
        print("OK: %s, version %s, image %d bytes, sha512 %s"
              % (args.file, version, len(image), hashlib.sha512(image).hexdigest()[:16] + "..."))


def read_image_bytes(image):
    if not image or image[0] != 0xE9:
        fail("the image in the file is not an ESP32 app image")
    return image


if __name__ == "__main__":
    main()
