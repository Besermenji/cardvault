#!/usr/bin/env python3
"""Checks the Ed25519 keys that SD firmware updates are verified with (#85).

  check_update_key.py                  src/platform/update_key.h matches tools/update_key.pub.pem
                                       (release) and tools/e2e/update_test_key.pub.pem (test); the
                                       release key is real (not the zero placeholder, not the test key)
  check_update_key.py --image app.bin  an app image holds the release key and not the test key
                                       (used by tools/check_release_image.sh)

Prints "FAIL: <reason>" lines on stderr and exits 1 on any problem. Standard library only.
"""
import base64
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "src", "platform", "update_key.h")
RELEASE_PEM = os.path.join(ROOT, "tools", "update_key.pub.pem")
TEST_PEM = os.path.join(ROOT, "tools", "e2e", "update_test_key.pub.pem")
ED25519_SPKI = bytes.fromhex("302a300506032b6570032100")  # DER prefix of an Ed25519 public key

problems = []


def pem_key(path):
    if not os.path.exists(path):
        problems.append("missing %s" % os.path.relpath(path, ROOT))
        return None
    text = open(path).read()
    m = re.search(r"-----BEGIN PUBLIC KEY-----(.*?)-----END PUBLIC KEY-----", text, re.S)
    der = base64.b64decode(m.group(1)) if m else b""
    if len(der) != 44 or not der.startswith(ED25519_SPKI):
        problems.append("%s is not an Ed25519 public key" % os.path.relpath(path, ROOT))
        return None
    return der[12:]


def header_keys():
    text = open(HEADER).read()
    arrays = re.findall(r"kUpdatePublicKey\[32\]\s*=\s*\{([^}]*)\}", text)
    if len(arrays) != 2 or text.find("#ifdef CV_UPDATE_TEST_KEY") > text.find("kUpdatePublicKey"):
        problems.append("update_key.h must hold the test key under #ifdef CV_UPDATE_TEST_KEY, then the release key")
        return None, None
    keys = [bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", a)) for a in arrays]
    if any(len(k) != 32 for k in keys):
        problems.append("update_key.h: a key is not 32 bytes")
        return None, None
    return keys[0], keys[1]  # test, release


def check_sources(test_h, release_h):
    test_pem, release_pem = pem_key(TEST_PEM), pem_key(RELEASE_PEM)
    if test_h is not None and test_pem is not None and test_h != test_pem:
        problems.append("the test key in update_key.h differs from tools/e2e/update_test_key.pub.pem")
    if release_h is not None:
        if release_h == bytes(32):
            problems.append("the release key in update_key.h is still the zero placeholder")
        elif release_h == test_h:
            problems.append("the release key in update_key.h is the test key")
        elif release_pem is not None and release_h != release_pem:
            problems.append("the release key in update_key.h differs from tools/update_key.pub.pem")


def check_image(path, test_h, release_h):
    app = open(path, "rb").read()
    if test_h is not None and test_h in app:
        problems.append("image holds the test update key (a test build?)")
    # With the placeholder there is nothing to look for; the source check fails on it.
    if release_h is not None and release_h != bytes(32) and release_h not in app:
        problems.append("image does not hold the release update key")


def main():
    test_h, release_h = header_keys()
    if len(sys.argv) == 3 and sys.argv[1] == "--image":
        check_image(sys.argv[2], test_h, release_h)
    elif len(sys.argv) == 1:
        check_sources(test_h, release_h)
    else:
        sys.exit(__doc__)

    for p in problems:
        print("FAIL: " + p, file=sys.stderr)
    if problems:
        sys.exit(1)
    print("OK: update keys" + (" in " + sys.argv[2] if len(sys.argv) == 3 else ""))


if __name__ == "__main__":
    main()
