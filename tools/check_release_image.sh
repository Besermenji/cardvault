#!/usr/bin/env bash
# Checks a merged flash image (tools/merge_bin.sh output) before it is released:
#   - the partition table puts the vault at 0x310000 (size 0x100000), never at
#     the test address 0x410000
#   - the app contains no test or debug code
# Used by CI, the release workflow and merge_bin.sh. Every problem is printed
# on stderr as "FAIL: <reason>"; exit status 1 if there was any.
#   usage: tools/check_release_image.sh <merged.bin>
set -euo pipefail

IMG="${1:?usage: check_release_image.sh <merged.bin>}"
[ -f "$IMG" ] || { echo "FAIL: no such image: $IMG" >&2; exit 1; }

fail=0
problem() { echo "FAIL: $*" >&2; fail=1; }

# Partition table: 32-byte entries at 0x8000, magic AA 50.
table=$(python3 -I - "$IMG" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as f:
    f.seek(0x8000)
    raw = f.read(0xC00)
for i in range(0, len(raw) - 31, 32):
    e = raw[i:i + 32]
    if e[:2] != b"\xaa\x50":
        break
    typ, sub, off, size = struct.unpack("<BBII", e[2:12])
    label = e[12:28].split(b"\0")[0].decode("ascii", "replace")
    print(f"{label} {typ} {sub} 0x{off:x} 0x{size:x}")
PY
)
vault=$(printf '%s\n' "$table" | awk '$1 == "vault"')
if [ -z "$vault" ]; then
    problem "no 'vault' partition in the partition table"
else
    read -r _ _ _ voff vsize <<<"$vault"
    [ "$voff" = "0x310000" ] || problem "vault partition at $voff, expected 0x310000"
    [ "$vsize" = "0x100000" ] || problem "vault partition size $vsize, expected 0x100000"
fi
[ "$(printf '%s\n' "$table" | awk '$1 == "vault"' | wc -l)" -le 1 ] || problem "more than one 'vault' partition"

# App image: everything from 0x10000.
app=$(mktemp)
trap 'rm -f "$app"' EXIT
tail -c +65537 "$IMG" > "$app"
for marker in CVSHOT CVTEST cvt_guard cvt_cfg "unknown command"; do
    if LC_ALL=C grep -a -q -- "$marker" "$app"; then
        problem "image contains test/debug marker: $marker"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "$IMG is NOT a release image" >&2
    exit 1
fi
echo "OK: $IMG (vault at 0x310000, no test/debug markers)"
