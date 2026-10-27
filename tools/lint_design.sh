#!/usr/bin/env bash
# Checks DESIGN.md against src/ui/theme.h and layout.h, then lints it with the design.md CLI.
# The spec is still alpha, so the CLI is pinned to an exact version in tools/design-lint/package.json
# and installed from its lockfile (integrity hashes for every package, install scripts disabled).
#   usage: tools/lint_design.sh
set -euo pipefail

cd "$(dirname "$0")/.."

python3 tools/check_design_sync.py

npm ci --prefix tools/design-lint --ignore-scripts --no-audit --no-fund --loglevel=error

# Exit status 1 means errors; warnings (e.g. contrast of the unquantised hex values) are printed only.
tools/design-lint/node_modules/.bin/designmd lint DESIGN.md
