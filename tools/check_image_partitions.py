"""Prints one line per problem with the partition table inside a merged flash image.

The image must hold the "vault", "app0" and "app1" partitions exactly as the release partition
CSV defines them and no test partition, and must end before the vault, so writing it can never
overwrite a vault. Used by tools/check_release_image.sh.
usage: check_image_partitions.py <merged.bin> <partitions.csv>
"""
import os, struct, sys

img, csv = sys.argv[1:3]
REQUIRED = ("vault", "app0", "app1")
# Every release since v0.1.0-rc1 keeps its vault here. Moving it would lose
# every user's vault on the next full-image install.
VAULT_SINCE_FIRST_RELEASE = (0x310000, 0x100000)
want = {}
for row in open(csv):
    cols = [c.strip() for c in row.split("#")[0].split(",")]
    if len(cols) >= 5 and cols[0] in REQUIRED:
        want[cols[0]] = (int(cols[3], 0), int(cols[4], 0))
missing = [name for name in REQUIRED if name not in want]
if missing:
    for name in missing:
        print("partitions.csv has no '%s' row" % name)
    sys.exit()
if want["vault"] != VAULT_SINCE_FIRST_RELEASE:
    print("partitions.csv moves the vault to 0x%x size 0x%x; released devices have it at 0x%x size 0x%x"
          % (*want["vault"], *VAULT_SINCE_FIRST_RELEASE))

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

for name in REQUIRED:
    if name not in table:
        print("no '%s' partition in the partition table" % name)
    elif len(table[name]) > 1:
        print("more than one '%s' partition" % name)
    elif table[name][0] != want[name]:
        print("%s partition at 0x%x size 0x%x, partitions.csv says 0x%x size 0x%x"
              % (name, *table[name][0], *want[name]))
if "cvtvault" in table:
    print("partition table has the test partition 'cvtvault'")
if os.path.getsize(img) > want["vault"][0]:
    print("image is 0x%x bytes and would overwrite the vault at 0x%x"
          % (os.path.getsize(img), want["vault"][0]))
