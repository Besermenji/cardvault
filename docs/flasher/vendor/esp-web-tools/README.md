# esp-web-tools 10.4.0 (vendored)

`dist/web/*` from the npm package `esp-web-tools@10.4.0` (Apache-2.0, see LICENSE), copied unchanged so the
flasher page loads no code from a third-party host.

- npm tarball integrity: `sha512-3pwkeFFm5Fj7UQo8SJNYK5RXrtNCpq6X9QoI6bMT4GBZWgrJqjn0YvM9ihG74BtMoSFYXfmDtkehuxe50PTMPQ==`
  (`npm view esp-web-tools@10.4.0 dist.integrity`)
- `../SHA256SUMS` lists every vendored file; `tools/check_flasher.sh` verifies it in CI and before a release.

To update: download the new tarball, check its integrity against the registry, replace the `*.js` files,
run `tools/check_flasher.sh --update`, and review the diff.
