# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed
- The vault only ever uses its own `vault` partition. The fallback to the shared `spiffs` partition is
  gone, so CardVault can run safely next to other firmware installed with Launcher.
- NVS namespaces renamed to `cv_guard` / `cv_cfg` to avoid collisions with other firmware.

### Security
- The 8-128 character passphrase policy is now enforced in the vault core (create, change passphrase,
  backup), not only in the UI; an empty passphrase is never accepted for unlock or restore. Vaults and
  backups made earlier with a shorter passphrase still open.
- The strength meter no longer rates repeats, runs, keyboard walks or word-like prose (lowercase words,
  however separated, capitalised or suffixed with digits) above "fair". Text made only of BIP39 words
  gets 11 bits per distinct word, which assumes they were generated; hand-picked BIP39 words are
  overrated.
- Restoring an SD backup while locked could reset the wrong-passphrase counter without replacing
  the vault, allowing unlimited guesses. The counter now resets only after a restore completes.
- Vault entries are deserialized in place so short secrets are not left in stack temporaries.

## [0.1.0] - 2026-10-20

### Added
- Encrypted vault: Argon2id + XChaCha20-Poly1305 (Monocypher 4.0.2), authenticated header, padding.
- Secret types: seed phrase (BIP39), password, 2FA (TOTP), note.
- BIP39 autocomplete, checksum validation, seed generation, paged seed viewer.
- Password / passphrase generator.
- 2FA overview with live codes; manual UTC clock.
- Attempt guard: escalating delays, wipe after 10 consecutive failures.
- Auto-lock with dimming, G0 instant lock, hold-to-reveal secrets.
- Encrypted SD backup and restore with read-back verification.
- Host unit tests, CI, release pipeline with merged image and web flasher.
