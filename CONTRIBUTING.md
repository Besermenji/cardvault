# Contributing to CardVault

Thanks for helping! CardVault guards people's secrets, so we hold changes to a high bar.

## Workflow

1. Open (or pick) an issue and discuss the approach first for anything non-trivial.
2. Branch from `main`: `feat/<issue>-short-slug`, `fix/<issue>-short-slug`, `docs/...`.
3. Use [Conventional Commits](https://www.conventionalcommits.org/): `feat(vault): ...`, `fix(ui): ...`, `test(crypto): ...`.
4. Keep commits small and focused. Each one should build.
5. Open a PR that references the issue (`Closes #n`). CI must be green.

## Building

```sh
pio run -e cardputer            # build firmware
pio run -e cardputer -t upload  # flash (hold G0 while powering on if needed)
pio test -e native              # run host unit tests
```

## Rules for security-sensitive code

- Never log secrets, passphrases, keys or decrypted content (not even in debug builds).
- Wipe secret buffers with `crypto_wipe` as soon as they are no longer needed.
- Code in `src/crypto`, `src/vault` and `src/features` must stay Arduino-free so it can be unit tested on the host.
- Changes to the on-disk format must bump the format version and keep reading old versions.
- Do not add any code that enables Wi-Fi or Bluetooth.
