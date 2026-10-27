"""Prints one line per problem with the partition table inside a merged flash image.

The image must hold the "vault" partition exactly as the release partition CSV defines it
and no test partition. Used by tools/check_release_image.sh.
usage: check_image_partitions.py <merged.bin> <partitions.csv>
"""
import struct, sys

img, csv = sys.argv[1:3]
want = None
for row in open(csv):
    cols = [c.strip() for c in row.split("#")[0].split(",")]
    if len(cols) >= 5 and cols[0] == "vault":
        want = (int(cols[3], 0), int(cols[4], 0))
if want is None:
    print("partitions.csv has no 'vault' row")
    sys.exit()

with open(img, "rb") as f:
    f.seek(0x8000)
    raw = f.read(0xC00)
table = {}
for i in range(0, len(raw) - 31, 32):
    e = raw[i:i + 32]
    if e[:2] != b"\xaa\x50":
        break
    off, size = struct.unpack("<II", e[4:12])
    table.setdefault(e[12:28].split(b"\0")[0].decode("ascii", "replace"), []).append((off, size))

if "vault" not in table:
    print("no 'vault' partition in the partition table")
elif len(table["vault"]) > 1:
    print("more than one 'vault' partition")
elif table["vault"][0] != want:
    print("vault partition at 0x%x size 0x%x, partitions.csv says 0x%x size 0x%x" % (*table["vault"][0], *want))
if "cvtvault" in table:
    print("partition table has the test partition 'cvtvault'")
