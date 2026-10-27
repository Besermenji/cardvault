#!/usr/bin/env python3
"""Fails if env:cardputer (the release environment) resolves to anything but a plain build.

Every option of the resolved environment is scanned, not only build_flags, and
every -D define must be on the allow-list below: -DCV_* flags enable test
hooks, screenshots, other NVS namespaces or shortened timeouts. The production
partition table must be used and PLATFORMIO_BUILD_FLAGS must be empty.
Run from the repository root.

  tools/check_release_flags.py [--project-dir DIR]   (the option is for testing the check itself)
"""
import json
import os
import re
import subprocess
import sys

ALLOWED_DEFINES = {"CORE_DEBUG_LEVEL=0", "ARDUINO_USB_CDC_ON_BOOT=1", "ARDUINO_USB_MODE=1"}


def flatten(value):
    if isinstance(value, (list, tuple)):
        for v in value:
            yield from flatten(v)
    else:
        yield str(value)


def check(cfg):
    problems = []
    for key, value in cfg.items():
        for text in flatten(value):
            for m in re.finditer(r"(?:^|\s)-D\s*(\S+)", text):
                if m.group(1) not in ALLOWED_DEFINES:
                    problems.append(f"unexpected define in env:cardputer {key}: -D{m.group(1)}")
            # A CV_ setting hiding in any other form (extra_scripts, a pre-built flag file, ...).
            if re.search(r"\bCV_", text) and not re.search(r"(?:^|\s)-D\s*\S*CV_", text):
                problems.append(f"CV_ setting in env:cardputer {key}: {text}")
    if cfg.get("board_build.partitions") != "partitions.csv":
        problems.append(f"partition table is {cfg.get('board_build.partitions')!r}, expected partitions.csv")
    if os.environ.get("PLATFORMIO_BUILD_FLAGS"):
        problems.append("PLATFORMIO_BUILD_FLAGS is set")
    return problems


def main(argv):
    cmd = ["pio", "project", "config", "--json-output"]
    if len(argv) == 3 and argv[1] == "--project-dir":
        cmd += ["-d", argv[2]]
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    cfg = dict(dict(json.loads(out))["env:cardputer"])
    problems = check(cfg)
    for p in problems:
        print("FAIL:", p, file=sys.stderr)
    if problems:
        return 1
    print("OK: env:cardputer has no unexpected -D defines and uses partitions.csv")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
