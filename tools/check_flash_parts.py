#!/usr/bin/env python3
"""Checks that the parts the web flasher installs keep an existing vault AND its attempt counter.

The merged image (written at 0x0) is padded with 0xFF over NVS, where the wrong-passphrase
counter lives. A missing counter allows one more try, so writing the merged image over a vault
leaves its owner a single attempt. The web flasher (and esptool upgrades) write four parts that
skip NVS instead. This checks, against docs/flasher/manifest.json:
  - the manifest installs exactly those four parts
  - no part's flash sectors touch NVS or reach the vault, and parts don't overlap
  - each part is byte for byte what the merged image has at that offset
  - the merged image holds nothing but 0xFF outside the parts (so nothing is left out)

usage: check_flash_parts.py <release_dir> <name> [--manifest manifest.json]
       <name> as written by tools/merge_bin.sh, e.g. cardvault-v0.4.0-cardputer-adv
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Manifest path (the file name on the flasher site) -> release file suffix.
PARTS = {"bootloader.bin": "-bootloader.bin", "partitions.bin": "-partitions.bin",
         "otadata.bin": "-otadata.bin", "firmware.bin": "-app-only.bin"}
SECTOR = 0x1000
NVS = (0x9000, 0xE000)  # the nvs row of partitions.csv
VAULT_START = 0x310000


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("release_dir")
    ap.add_argument("name")
    ap.add_argument("--manifest", default=os.path.join(ROOT, "docs", "flasher", "manifest.json"))
    args = ap.parse_args()
    problems = []

    manifest = json.load(open(args.manifest))
    builds = manifest.get("builds", [])
    listed = [(p.get("path"), p.get("offset")) for b in builds for p in b.get("parts", [])]
    if len(builds) != 1 or sorted(str(p) for p, _ in listed) != sorted(PARTS):
        problems.append("the flasher manifest must install exactly %s, it installs %s"
                        % (", ".join(sorted(PARTS)), ", ".join(str(p) for p, _ in listed)))
    else:
        merged = open(os.path.join(args.release_dir, args.name + ".bin"), "rb").read()
        left = bytearray(merged)
        used = []
        for path, offset in sorted(listed, key=lambda x: x[1]):
            data = open(os.path.join(args.release_dir, args.name + PARTS[path]), "rb").read()
            start, end = offset, offset + len(data)
            first, last = start // SECTOR * SECTOR, (end + SECTOR - 1) // SECTOR * SECTOR  # erased
            if first < NVS[1] and last > NVS[0]:
                problems.append("%s at 0x%x erases part of NVS (the attempt counter)" % (path, offset))
            if last > VAULT_START:
                problems.append("%s at 0x%x reaches the vault at 0x%x" % (path, offset, VAULT_START))
            if any(first < u_last and last > u_first for u_first, u_last in used):
                problems.append("%s at 0x%x shares flash sectors with another part" % (path, offset))
            used.append((first, last))
            if merged[start:end] != data:
                problems.append("%s differs from the merged image at 0x%x" % (path, offset))
            left[start:end] = b"\xff" * len(left[start:end])
        if left.count(0xFF) != len(left):
            first_bad = next(i for i, b in enumerate(left) if b != 0xFF)
            problems.append("the merged image has data at 0x%x that no part installs" % first_bad)

    for p in problems:
        print("FAIL: " + p, file=sys.stderr)
    if problems:
        sys.exit(1)
    print("OK: flasher parts match %s.bin and leave NVS and the vault alone" % args.name)


if __name__ == "__main__":
    main()
