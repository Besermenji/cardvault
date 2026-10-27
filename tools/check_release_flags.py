#!/usr/bin/env python3
"""Fails if env:cardputer (the release environment) resolves to anything but a plain build.

No -DCV_* flag may be set (they enable test hooks, screenshots, other NVS
namespaces or shortened timeouts), the production partition table must be used
and PLATFORMIO_BUILD_FLAGS must be empty. Run from the repository root.
"""
import json
import os
import subprocess
import sys

out = subprocess.run(["pio", "project", "config", "--json-output"], check=True,
                     capture_output=True, text=True).stdout
sections = json.loads(out)
cfg = dict(dict(sections)["env:cardputer"])
flags = cfg.get("build_flags", [])
problems = []
if os.environ.get("PLATFORMIO_BUILD_FLAGS"):
    problems.append("PLATFORMIO_BUILD_FLAGS is set")
problems += [f"test/debug flag in env:cardputer: {f}" for f in flags if f.startswith("-DCV_")]
if cfg.get("board_build.partitions") != "partitions.csv":
    problems.append(f"partition table is {cfg.get('board_build.partitions')!r}, expected partitions.csv")
for p in problems:
    print("FAIL:", p, file=sys.stderr)
if problems:
    sys.exit(1)
print("OK: env:cardputer has no -DCV_* flags and uses partitions.csv")
