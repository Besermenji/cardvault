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

## Build environments

- `cardputer` is the only release environment. It may only define the `-D` flags on the allow-list in
  `tools/check_release_flags.py` (no `-DCV_*`); CI checks this.
- `cardputer-debug` (screenshots), `cardputer-e2e` (test hooks) and `cardputer-test` use the test partition
  table, whose vault partition is labelled `cvtvault` instead of `vault`, and their own NVS namespaces.
  They cannot open a real vault. Flash them with `pio run -e <env> -t upload`, which writes the partition
  table too. `CV_TEST_DELAY_DIV` and `CV_REVEAL_MS` are only accepted together with `CV_TEST_HOOKS`.
- When `partitions.csv` changes, `tools/check_release_image.sh` follows it (it reads the vault row from there).
- `tools/merge_bin.sh <version> <out_dir> <env>` can merge other environments for flashing, but names the
  result `...-NOT-FOR-RELEASE.bin`.

## Rules for security-sensitive code

- Release builds (`cardputer`) never write secrets, passphrases, keys or decrypted content anywhere.
  Do not add logging of them.
- The test builds do, on purpose: `cardputer-e2e` has a `dump` command that prints every entry over
  serial, and `cardputer-debug` streams the screen (including anything revealed). That is why they
  run on the `cvtvault` partition and their own NVS namespaces, and must never be used with real
  secrets. Code that exposes secret data must stay behind `CV_TEST_HOOKS` / `CV_SCREENSHOT`, and
  CI checks that release images contain none of it.
- Wipe secret buffers with `crypto_wipe` as soon as they are no longer needed.
- Code in `src/crypto`, `src/vault` and `src/features` must stay Arduino-free so it can be unit tested on the host.
- Changes to the on-disk format must bump the format version and keep reading old versions.
- Do not add any code that enables Wi-Fi or Bluetooth.
