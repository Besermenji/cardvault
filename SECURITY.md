# Security Policy

## Reporting a vulnerability

Please **do not** open a public issue for security problems.
Use GitHub's private vulnerability reporting ("Security" tab → "Report a vulnerability")
on this repository. We aim to respond within 7 days.

## Threat model (summary)

CardVault protects secrets stored on an M5Stack Cardputer ADV.

**Protects against**
- Someone who picks up a locked device and tries to guess the passphrase *on the device*:
  each attempt costs ~2 s of Argon2id, delays grow after failures, and the vault is
  erased after 10 consecutive failures.
- Casual tampering with the vault file: every byte is authenticated; modified files are rejected.
- Remote attacks: the firmware never turns on Wi-Fi or Bluetooth. The radio drivers are linked in
  with the Arduino framework but are never started.

The Argon2id cost is time-hard and only lightly memory-hard (128 KiB on a Cardputer ADV). One core of
an Apple M1 running the same code computes about 120 guesses per second (measured 2026-10-27),
about 240× the device's speed, so everything below that allows offline guessing depends on the
passphrase alone.

**Does NOT protect against**
- An attacker with physical access who dumps the flash chip (or reflashes the device) and
  brute-forces the passphrase **offline**. Without ESP32 secure boot + flash encryption
  (irreversible eFuse changes, not enabled by default), the attempt limit can be bypassed
  this way. **A long, random passphrase is your real protection** (e.g. 5+ diceware words).
- Malicious firmware flashed onto your device before you type your passphrase. This includes other
  firmware installed next to CardVault with Launcher: it can read the encrypted vault (enabling offline
  guessing) and reset the attempt counter stored in the shared NVS partition.
- Someone who gets a copy of an SD backup (`/cardvault/*.cvb`). It is encrypted and authenticated, but
  offers no attempt limit and the same offline-guessing cost as a flash dump. Give backups their own
  long random passphrase; reusing the vault passphrase makes every copy a second target for it. This
  includes the safety copy that *Compact vault* writes to the card: it is a normal backup under the
  vault passphrase, so delete it on a computer once the compaction is done, or keep the card as safe as
  the device.
- Old encrypted copies left in flash. Every save leaves the previous version behind until its
  space is reused; after a passphrase change or restore, such a copy still opens with the old
  passphrase. Settings › *Compact vault* erases them (it is offered after both).
- Someone watching your screen or keyboard while you unlock or reveal secrets.
- Losing the device without a backup. Always keep an independent backup of seed phrases.
- Someone destroying your vault on purpose: anyone holding the device can trigger the 10-failure
  wipe. Keep SD backups.
- Rollback: an older valid vault or backup being put back (by someone writing the flash, or by you
  restoring an old backup) is not detected.
- A wipe covers the vault partition only. SD backups and the settings (auto-lock, brightness) survive it.
- Secrets left in RAM while unlocked. Wiping on lock is best effort: the keyboard library's key
  buffer, the screen frame buffer (until the next redraw) and short copies of small strings made by
  library or UI code are not covered (see
  [docs/security-review-2026-10.md](docs/security-review-2026-10.md)). A power cycle clears RAM.
- Anything another firmware does. Launcher behaviour is Launcher's; CardVault was built for
  Launcher 2.9.1 but that is not yet verified on hardware (#28).

See the README for the full design and [docs/HARDENING.md](docs/HARDENING.md) for optional eFuse hardening.
