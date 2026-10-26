#!/usr/bin/env python3
"""Run the on-device test suite and capture its output.

  pio test -e cardputer-test --without-testing      # build + upload only
  python3 tools/device_test.py /dev/cu.usbmodem101

`pio test` cannot follow the Cardputer's native USB port: it disappears
while the board resets, and the run errors with "Device not configured".
This script resets the board through RTS (as the e2e harness does), waits
for the port to come back, and prints the Unity output until the summary.
Exit code: 0 all passed, 1 failures, 2 no complete run (reset or timeout).
"""
import argparse
import re
import sys
import time

import serial

SUMMARY = re.compile(r"^(\d+) Tests (\d+) Failures (\d+) Ignored")


def open_port(port, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 0.2
            s.dtr = False  # default DTR/RTS can put the S3 into download mode
            s.rts = False
            s.open()
            return s
        except (serial.SerialException, OSError):
            time.sleep(0.1)
    raise SystemExit(f"{port} did not come back within {timeout} s")


def reset(port):
    s = open_port(port, 10)
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    s.close()
    time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--no-reset", action="store_true", help="attach to a run already starting")
    ap.add_argument("--timeout", type=float, default=900, help="seconds for the whole run")
    args = ap.parse_args()

    if not args.no_reset:
        reset(args.port)
    ser = open_port(args.port, 20)
    end = time.time() + args.timeout
    buf = b""
    summary = None
    while time.time() < end:
        try:
            buf += ser.read(512)
        except (serial.SerialException, OSError):
            print("\n!! port lost mid-run: the device reset (crash or watchdog?)", flush=True)
            return 2
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip("\r")
            print(line, flush=True)
            if summary:
                return 1 if summary[1] or line.strip() == "FAIL" else 0
            m = SUMMARY.match(line)
            if m:
                summary = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
    print(f"\n!! no Unity summary within {args.timeout:.0f} s", flush=True)
    return 2


if __name__ == "__main__":
    sys.exit(main())
