# Security Policy

## Reporting a vulnerability

Please **do not** open a public issue for security problems.
Use GitHub's private vulnerability reporting ("Security" tab → "Report a vulnerability")
on this repository. We aim to respond within 7 days.

## Threat model (summary)

CardVault protects secrets stored on an M5Stack Cardputer ADV.

**Protects against**
- Someone who picks up a locked device and tries to guess the passphrase on the device:
  each attempt costs ~2 s of Argon2id, delays grow after failures, and the vault is
  erased after 10 consecutive failures.
- Someone who steals the SD card backup: it is encrypted with XChaCha20-Poly1305 under a
  key derived from your passphrase with Argon2id.
- Tampering with the vault file: every byte is authenticated; modified files are rejected.
- Remote attacks: Wi-Fi and Bluetooth are never enabled by the firmware.

**Does NOT protect against**
- An attacker with physical access who dumps the flash chip (or reflashes the device) and
  brute-forces the passphrase **offline**. Without ESP32 secure boot + flash encryption
  (irreversible eFuse changes, not enabled by default), the attempt limit can be bypassed
  this way. **A long, random passphrase is your real protection** (e.g. 5+ diceware words).
- Malicious firmware flashed onto your device before you type your passphrase.
- Someone watching your screen or keyboard while you unlock or reveal secrets.
- Losing the device without a backup. Always keep an independent backup of seed phrases.

See the README for the full design.
