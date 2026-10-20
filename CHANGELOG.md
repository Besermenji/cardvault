# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

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
