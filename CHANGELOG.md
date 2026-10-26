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
- The wrong-passphrase counter now fails closed. If its value is missing or unreadable (NVS erased
  while the vault stays) the device starts at 5 failures instead of 0, and if the counter storage
  cannot be opened at boot, unlocking is disabled with an explanation. **After upgrading you may see
  "5 attempts left" once** (the counter was renamed in an earlier release); one correct unlock
  resets it.
- Only storage and out-of-memory errors give an attempt back; every other error after the passphrase
  check ran stays counted. Reading the vault file or its header costs no attempt. A vault whose
  passphrase is right but whose payload cannot be read (corrupt or newer format) now counts as a
  correct passphrase and says why, instead of refunding one attempt.
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
