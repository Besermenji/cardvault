#!/usr/bin/env bash
# Checks the web flasher source (docs/flasher):
#   - the vendored esp-web-tools files match docs/flasher/vendor/SHA256SUMS
#   - index.html loads nothing from another host (script, style, image, ...)
#   - index.html carries a Content-Security-Policy that allows nothing but same-origin sources
#   usage: tools/check_flasher.sh [--update]    (--update rewrites SHA256SUMS)
set -euo pipefail
TOOLS="$(cd "$(dirname "$0")" && pwd)"
cd "$TOOLS/../docs/flasher"

if command -v sha256sum >/dev/null; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
list() { (cd vendor && find esp-web-tools -type f ! -name README.md | LC_ALL=C sort | xargs $SHA); }

if [ "${1:-}" = "--update" ]; then
    list > vendor/SHA256SUMS
    echo "updated vendor/SHA256SUMS"
    exit 0
fi

fail=0
problem() { echo "FAIL: $*" >&2; fail=1; }

[ -f vendor/SHA256SUMS ] || problem "vendor/SHA256SUMS is missing"
if [ -f vendor/SHA256SUMS ]; then
    diff <(list) vendor/SHA256SUMS >/dev/null || problem "vendored files differ from vendor/SHA256SUMS"
fi

# src=, href= (except plain links to other pages) and CSS url() pointing at http(s):// or //.
if grep -nE '(src|srcset|data)=["'"'"']?(https?:)?//' index.html manifest.json; then
    problem "index.html loads a resource from another host"
fi
if grep -nE '<link[^>]+href=["'"'"']?(https?:)?//' index.html; then
    problem "index.html links a stylesheet or icon from another host"
fi
if grep -nE 'url\(["'"'"']?(https?:)?//|@import' index.html; then
    problem "index.html pulls CSS from another host"
fi
# The policy itself: only same-origin sources, inline code never allowed.
while IFS= read -r line; do problem "$line"; done < <(python3 -I "$TOOLS/check_csp.py" index.html)

[ "$fail" -eq 0 ] || exit 1
echo "OK: flasher vendor files match SHA256SUMS, no external resources, strict CSP"
