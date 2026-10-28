# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Boot splash (#74): a vault-door dial turns and settles, with the name, tagline and version, for
  1.5 s at power-on. Any key skips it and is not typed into the passphrase prompt. Locking and
  unlocking are unchanged. It is also the M5Burner cover now (`tools/screenshots.py` captures it);
  e2e t30 covers the timing and the swallowed key.
- Screenshots in the README and an M5Burner cover (#19). `tools/screenshots.py` drives a Cardputer
  running the e2e build through the real UI with made-up data and saves a PNG of each main screen;
  the e2e build got a `shot` command for it (e2e t27). Release images are unchanged.
- Support section with BTC/ETH donation addresses and QR codes in the README, the web flasher,
  release notes and the M5Burner description, plus a GitHub Sponsor button (#54). The addresses live
  in `docs/support/addresses.env`; `tools/support.py` (run in CI and before every release) checks their checksums, the QR codes
  and every copy.

### Fixed
- Secret hygiene (#39). A revealed secret hides again after 20 s, when the screen dims and when you
  come back to the entry; ENTER on a secret row reveals just that row and does nothing on
  Username/URL (R still reveals all, holding Space still peeks). The unlock, setup and restore
  screens use the auto-lock time too: when idle, typed passphrases are wiped, open prompts close and
  the screen dims. Strings are wiped over their whole buffer (inline buffer and leftover bytes
  included), secrets are no longer copied for display, and the freed slot after deleting an entry is
  zeroed. Stored auto-lock and brightness are clamped on load (30 s to 30 min, 20 to 255), so a bad
  value cannot make the device unusable. The generator shows the whole password (up to 64
  characters, 12 words); the vault shows how full it is (Settings > About, and in the list header
  from 75 %) and a save that would not fit says by how many bytes.
- Destructive and sloppy input paths now ask or validate (#38). Esc in a text input whose text
  differs from what it started with asks before discarding it, masked secrets included (passphrase
  prompts and typing ERASE still close at once). A free-text seed must contain a visible character
  and the 2FA secret is masked while typing (OPT shows it). Saving or deleting an entry puts memory
  back exactly as it was if writing to flash fails. The editor compares against what is stored, so
  undoing an edit leaves nothing to discard, while a generated password you have not saved asks
  before Esc drops it. Set clock starts empty when no time was set (no invented date), accepts only
  `YYYY-MM-DD HH:MM[:SS]`, refuses dates before 2025-01-01, and gives a refused value back for
  fixing. Keys held or queued when the screen changes no longer reach the next screen.
- Unlocking now really costs ~2 s of Argon2id (#37). Setup used to extrapolate from one cold timing
  and landed near 0.7 s. It now separates the fixed cost from the per-pass cost and checks the
  vault's real key derivation once. Argon2 memory is sized from the free heap, and Change
  passphrase re-tunes (never with less memory than before), so older vaults get the full cost too.
- Reading an SD card no longer freezes the device when the card does not answer (#43): the mount
  is bounded and reports "SD card not readable". The card now uses the same SPI wiring as M5's
  Cardputer example. Backups never overwrite an older file, sort numerically past 9999, and the
  card is released after every backup, restore or cancel.

### Changed
- The vault only ever uses its own `vault` partition. The fallback to the shared `spiffs` partition is
  gone, so CardVault no longer shares storage with other firmware installed with Launcher. Other
  firmware can still read the encrypted vault and reset the attempt counter.
- NVS namespaces renamed to `cv_guard` / `cv_cfg` to avoid collisions with other firmware. On upgrade
  from v0.1.0-rc1 the settings (auto-lock, brightness) go back to their defaults once.

### Security
- Release and CI hardening (#40). The release check (`tools/check_release_image.sh`) runs on the
  merged image that ships: the vault partition must be at 0x310000 and no test or debug code may be
  in it; CI proves the check fails on the e2e and debug images, and `merge_bin.sh` refuses
  `PLATFORMIO_BUILD_FLAGS`. `CV_TEST_DELAY_DIV` and `CV_REVEAL_MS` are a compile error without
  `CV_TEST_HOOKS`. The e2e, debug and device-test builds use a partition table whose vault partition is
  labelled `cvtvault` (and their own NVS namespaces), so they cannot open a real vault, and the
  release check rejects an image that has that partition or label. The web flasher serves
  its own pinned copy of esp-web-tools under a Content-Security-Policy, and shows the image's SHA-256.
  Releases need a tag on `main` that CI built successfully and release notes (a prerelease uses its
  own section or `[Unreleased]`, never an earlier release's), are built without write access, get a build attestation once the repo is public, and prerelease tags no longer replace the
  public flasher. GitHub Actions are pinned by commit SHA; M5Unified, M5GFX and IRremote are pinned.
- Vault files and SD backups are checked (header, size, padding) before any key derivation runs and
  before memory is allocated from their size, so an oversize or malformed file costs no attempt and
  cannot exhaust memory. The Argon2 work a file may request (memory × passes) is capped, so a crafted
  backup cannot keep the device busy for more than about 10 s. Calibrated vaults use a small fraction
  of that (~21000 of 100000 for a 2 s unlock on a Cardputer ADV).
- Saving never deletes the current vault before the new one is in place, and a half-written new
  version left by a power cut is no longer promoted at boot.
- New *Compact vault* setting removes old encrypted copies from flash (it first saves a verified safety
  copy to SD). It is offered after a passphrase change or restore, when an old copy would still open
  with the old passphrase.
- The wrong-passphrase counter now fails closed. If no counter is stored while a vault exists (NVS
  erased or damaged), the device allows one more attempt, after a 5-minute wait, instead of starting
  from 0; a correct unlock restores a normal counter. A counter that cannot be read refuses unlocking
  instead of reading as 0, and if the counter storage cannot be opened at boot, unlocking is disabled
  with an explanation. The counter kept by v0.1.0-rc1 is carried over on upgrade, and only when a vault already exists, so a
  fresh install never reads another firmware's generic `guard` namespace.
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

### Documentation
- `DESIGN.md` describes the on-device UI (colours, fonts, the 240x135 grid, components, rules for new
  screens) in the design.md format (#66). `tools/check_design_sync.py` (run in CI) validates its front
  matter and token references, checks the quoted contrast ratios, and checks its colours, fonts, geometry and glyph
  cells against `src/ui/theme.h` and `layout.h`.
- The security docs now say what is and is not protected (#41). SD backups can be guessed offline
  with no attempt limit (README no longer suggests reusing the vault passphrase for them); the Argon2
  cost is ~2 s on the device only (about 120 guesses/s on one Apple M1 core) and only lightly
  memory-hard; a wipe covers the vault partition only; anyone holding the device can trigger the
  10-failure wipe; rollback to an older valid vault or backup is not detected; wiping RAM is best
  effort; the radios are linked but never turned on; Launcher behaviour is Launcher's and is not yet
  verified on hardware; *Compact vault* leaves an ordinary backup under the vault passphrase on the
  card; the e2e and debug builds do print secrets (that is what the `cvtvault`
  partition is for). The security review notes were brought up to date.

### Tests
- The last two manual hardware checks are automated (#17). e2e: the `keys` hook holds keys by their
  keyboard matrix position and adds them to the real key list, so fn/shift state, the library keymap,
  key rolls and auto-repeat all run as for a physical key (it replaces the character-level `press`
  hook); `keyseq` runs a timed sequence of such sets on the device, so hold times do not depend on
  the serial round trip; t28 types every key unshifted and shifted, rolls, fn arrows, repeat, Esc, hold-to-reveal and
  Opt. The `sdcut` hook simulates pulling the SD card (MISO held high, now or N ms into the next
  backup write or read); t29 checks that backup and restore end in an error message, that no
  attempt is counted, that G0 locks afterwards and that a half-written backup is refused. Not
  covered: the I2C read of the keyboard and the switches themselves.
- Regression coverage for the audit findings (#42). Host: the vendored Argon2id is checked against
  the RFC 9106 vector and our wrapper against libargon2; freed `SString`/`SBytes`/`SVec` buffers
  and `SecretBox`/`SecretKey` are checked to be zero (a heap watcher sees each block as it is
  freed); every header field (version, KDF id, memory, passes, work cap) is tampered with and must
  be refused without deriving a key and without costing an attempt. e2e: serial requests carry
  ids, so a late reply can no longer be taken for the next answer, and a dropped link fails the run
  instead of being retried silently; a `storage` hook reads the real vault files, scans the raw
  partition for leftover vault bytes and reads the attempt counter, so the wipe checks (factory,
  ten wrong passphrases, ERASE) prove the flash was emptied; new scenarios for TAB on every screen,
  an empty backup passphrase, confirmed clearing of a stored field and the Set clock default.

## [0.1.0-rc1] - 2026-10-20

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
