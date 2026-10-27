#!/usr/bin/env bash
# Prints the release notes for a tag from CHANGELOG.md; fails if there are none.
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
