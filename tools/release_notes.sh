#!/usr/bin/env bash
# Prints the release notes for a tag from CHANGELOG.md, then a "How to update"
# block (docs/UPDATING.md); fails if the changelog has no notes for the tag.
#   final tag  v0.1.0      -> the "## [0.1.0]" section
#   prerelease v0.1.0-rc2  -> "## [0.1.0-rc2]" if there is one, else "## [Unreleased]"
# A prerelease never falls back to its base version's section: that one holds the
# notes of an earlier release candidate or of the final release, not what is new.
#   usage: tools/release_notes.sh v0.1.0 > notes.md
set -euo pipefail

TAG="${1:?usage: release_notes.sh <tag>}"
v="${TAG#v}"
cd "$(dirname "$0")/.."

section() {  # <heading> -> body of that section ("" if absent)
    awk -v h="## [$1]" 'index($0, h) == 1 {f=1; next} /^## \[/ {f=0} f' CHANGELOG.md
}
nonempty() { printf '%s\n' "$1" | grep -q '[^[:space:]]'; }

notes=$(section "$v")
where="## [$v]"
if ! nonempty "$notes" && [[ "$v" == *-* ]]; then
    notes=$(section "Unreleased")
    where="## [$v] or ## [Unreleased]"
fi
if ! nonempty "$notes"; then
    echo "CHANGELOG.md has no non-empty $where section for $TAG; write the release notes before tagging" >&2
    exit 1
fi
printf '%s\n' "$notes"

# The first version with the second app slot (#85). Devices on anything older need
# one full install before SD updates work; later releases say so only in passing.
FIRST_SD_UPDATE="0.4.0"
base="${v%%-*}"
if [ "$(printf '%s\n%s\n' "$base" "$FIRST_SD_UPDATE" | sort -V | head -n1)" != "$FIRST_SD_UPDATE" ]; then
    exit 0  # older than SD updates: nothing to add
fi
guide="https://github.com/Besermenji/cardvault/blob/main/docs/UPDATING.md"
img="cardvault-${TAG}-cardputer-adv"
parts="0x0 ${img}-bootloader.bin 0x8000 ${img}-partitions.bin 0xe000 ${img}-otadata.bin 0x10000 ${img}-app-only.bin"
usb_upgrade() {
    printf -- '  - web flasher, with *Erase device* unticked, or\n'
    printf -- '  - `esptool.py --chip esp32s3 write_flash %s`\n' "$parts"
}
printf '\n### How to update\n\n'
printf -- '**Make an SD backup first.**\n\n'
if [ "$base" = "$FIRST_SD_UPDATE" ]; then
    printf -- '- **Everyone** (once, over USB; your vault is kept). Use one of:\n'
    usb_upgrade
    printf -- '  From this version on, later versions can be installed from the SD card (`%s.cvu`, Settings > Update firmware).\n' "$img"
else
    printf -- '- **From v%s or newer:** copy `%s.cvu` into `/cardvault` on the SD card, then Settings > Update firmware.\n' "$FIRST_SD_UPDATE" "$img"
    printf -- '- **From an older version** (once, over USB; your vault is kept). Use one of:\n'
    usb_upgrade
fi
printf -- '- **Installed through Launcher:** update from Launcher.\n'
printf -- '\n> **M5Burner, or `%s.bin` written at 0x0:** this erases the wrong-passphrase counter. Over an existing vault you then get **one** passphrase attempt, after a 5-minute wait, and **a typo erases the vault**. Press <kbd>opt</kbd> to show what you type and check it before Enter.\n' "$img"
printf -- '\nStep by step: [docs/UPDATING.md](%s).\n' "$guide"
