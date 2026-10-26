# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- Reading an SD card no longer freezes the device when the card does not answer (#43): the mount
  is bounded and reports "SD card not readable". The card now uses the same SPI wiring as M5's
  Cardputer example. Backups never overwrite an older file, sort numerically past 9999, and the
  card is released after every backup, restore or cancel.

### Changed
- The vault only ever uses its own `vault` partition. The fallback to the shared `spiffs` partition is
  gone, so CardVault can run safely next to other firmware installed with Launcher.
- NVS namespaces renamed to `cv_guard` / `cv_cfg` to avoid collisions with other firmware.

### Security
- Vault files and SD backups are checked (header, size, padding) before any key derivation runs and
  before memory is allocated from their size, so an oversize or malformed file costs no attempt and
  cannot exhaust memory. The Argon2 work a file may request (memory × passes) is capped, so a crafted
  backup cannot keep the device busy for minutes; every vault made so far is within the cap.
- Saving never deletes the current vault before the new one is in place, and a half-written new
  version left by a power cut is no longer promoted at boot.
- New *Compact vault* setting removes old encrypted copies from flash (it first saves a verified safety
  copy to SD). It is offered after a passphrase change or restore, when an old copy would still open
  with the old passphrase.
- The wrong-passphrase counter now fails closed. If no counter is stored while a vault exists (NVS
  erased or damaged), the device allows one more attempt, after a 5-minute wait, instead of starting
  from 0; a correct unlock restores a normal counter. A counter that cannot be read refuses unlocking
  instead of reading as 0, and if the counter storage cannot be opened at boot, unlocking is disabled
  with an explanation. The counter kept by v0.1.0-rc1 is carried over on upgrade.
- Only storage and out-of-memory errors give an attempt back; every other error after the passphrase
  check ran stays counted. Reading the vault file or its header costs no attempt. A vault whose
  passphrase is right but whose payload cannot be read now counts as a correct passphrase, and the
  message says what is wrong (corrupt data, newer format or unsupported settings), instead of
  refunding one attempt.
- A failed erase no longer resets the wrong-passphrase counter. Every wipe (10 wrong passphrases or
  "Erase vault") first saves the counter as exhausted and resets it only after the erase succeeds;
  a failed or interrupted wipe stops on an error screen and is retried on the next boot, even when
  the storage no longer mounts.
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
