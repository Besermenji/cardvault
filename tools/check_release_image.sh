#!/usr/bin/env bash
# Checks a merged flash image (tools/merge_bin.sh output) before it is released:
#   - the partition table in the image has the "vault" partition exactly as
#     partitions.csv (the release table) defines it, and no test partition
#   - the app contains no test or debug code
# Used by CI, the release workflow and merge_bin.sh. Every problem is printed
# on stderr as "FAIL: <reason>"; exit status 1 if there was any.
#   usage: tools/check_release_image.sh <merged.bin>
set -euo pipefail

IMG="${1:?usage: check_release_image.sh <merged.bin>}"
[ -f "$IMG" ] || { echo "FAIL: no such image: $IMG" >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

fail=0
problem() { echo "FAIL: $*" >&2; fail=1; }

# Partition table: 32-byte entries at 0x8000, magic AA 50. The expected vault
# row comes from partitions.csv, the source of truth for release builds.
while IFS= read -r line; do problem "$line"; done < <(python3 -I "$ROOT/tools/check_image_partitions.py" "$IMG" "$ROOT/partitions.csv")

# App image: everything from 0x10000.
app=$(mktemp)
trap 'rm -f "$app"' EXIT
tail -c +65537 "$IMG" > "$app"
for marker in CVSHOT CVTEST cvt_guard cvt_cfg cvtvault "unknown command"; do
    if LC_ALL=C grep -a -q -- "$marker" "$app"; then
        problem "image contains test/debug marker: $marker"
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "$IMG is NOT a release image" >&2
    exit 1
fi
echo "OK: $IMG (vault partition as in partitions.csv, no test/debug markers)"
