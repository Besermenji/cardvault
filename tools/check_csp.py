"""Prints one line per problem with the Content-Security-Policy meta tag of an HTML page.

Only same-origin sources are allowed; inline code is never allowed for scripts.
Used by tools/check_flasher.sh.   usage: check_csp.py index.html
"""
import re, sys

html = open(sys.argv[1]).read()
metas = re.findall(r'<meta[^>]+http-equiv=["\']Content-Security-Policy["\'][^>]*>', html, re.I)
if len(metas) != 1:
    print("index.html needs exactly one Content-Security-Policy meta tag, found %d" % len(metas))
    sys.exit()
m = re.search(r'content="([^"]*)"', metas[0])
if not m:
    print("the Content-Security-Policy meta tag has no content")
    sys.exit()
policy = {}
for part in m.group(1).split(";"):
    toks = part.split()
    if toks:
        policy.setdefault(toks[0].lower(), toks[1:])
# Per directive, the only sources that may appear.
allowed = {"'none'", "'self'"}
extra = {"style-src": {"'unsafe-inline'"}, "img-src": {"data:"}, "font-src": {"data:"}}
for name, toks in policy.items():
    for t in toks:
        if t not in allowed | extra.get(name, set()):
            print("CSP %s allows %r" % (name, t))
if policy.get("default-src") not in (["'none'"], ["'self'"]):
    print("CSP default-src must be 'none' or 'self', is %r" % policy.get("default-src"))
if policy.get("script-src") != ["'self'"]:
    print("CSP script-src must be exactly 'self', is %r" % policy.get("script-src"))
for name in ("base-uri", "form-action"):
    if policy.get(name) != ["'none'"]:
        print("CSP %s must be 'none'" % name)
