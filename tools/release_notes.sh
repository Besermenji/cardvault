#!/usr/bin/env bash
# Prints the CHANGELOG.md section for a release tag; fails if it is missing or empty.
# The base version is used, so v0.1.0-rc2 reads "## [0.1.0]".
#   usage: tools/release_notes.sh v0.1.0 > notes.md
set -euo pipefail

TAG="${1:?usage: release_notes.sh <tag>}"
v="${TAG#v}"
v="${v%%-*}"
cd "$(dirname "$0")/.."

notes=$(awk -v v="$v" 'index($0, "## [" v "]") == 1 {f=1; next} /^## \[/ {f=0} f' CHANGELOG.md)
if ! printf '%s\n' "$notes" | grep -q '[^[:space:]]'; then
    echo "CHANGELOG.md has no non-empty '## [$v]' section; move the [Unreleased] notes under it before tagging" >&2
    exit 1
fi
printf '%s\n' "$notes"
