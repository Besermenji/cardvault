#!/usr/bin/env bash
# Lints DESIGN.md with the design.md CLI and checks it against src/ui/theme.h and layout.h.
# The spec is still alpha, so the CLI version is pinned here and only here.
#   usage: tools/lint_design.sh
set -euo pipefail

DESIGN_MD_VERSION="0.4.0"

cd "$(dirname "$0")/.."

python3 tools/check_design_sync.py

# Exit status 1 means errors; warnings (e.g. contrast of the unquantised hex values) are printed only.
npx --yes "@google/design.md@${DESIGN_MD_VERSION}" lint DESIGN.md
