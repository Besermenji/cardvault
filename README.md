# CardVault

**An offline, encrypted secrets vault firmware for the M5Stack Cardputer ADV.**

Keep BIP39 seed phrases, passwords, 2FA (TOTP) secrets and private notes on your Cardputer,
encrypted with a passphrase only you know. No Wi-Fi. No Bluetooth. No cloud.

> ⚠️ **Early software (v0.x).** Always keep an independent backup (paper/metal) of any seed phrase.
> Read the [threat model](#security-model) before trusting CardVault with real funds.

## Features

| | |
|---|---|
| 🔐 **Strong encryption** | Argon2id key derivation (tuned to ~2 s per guess on the device) + XChaCha20-Poly1305 via [Monocypher](https://monocypher.org) |
| 🧠 **BIP39 helper** | Type seed words with autocomplete (4 letters are always enough), checksum validation, generate new 12/24-word seeds with the hardware RNG, view words page by page |
| 🔑 **Password generator** | Random characters or BIP39-word passphrases, unbiased sampling, entropy shown |
| ⏱️ **2FA codes** | RFC 6238 TOTP (6/8 digits, 30/60 s) with a live overview of all your codes |
| 💾 **SD backups** | Encrypted, verified backups to `/cardvault/*.cvb` on the microSD card, restorable on any Cardputer |
| 🛡️ **Brute-force guard** | Escalating delays; **10 wrong passphrases in a row erase the vault** |
| 🙈 **Privacy** | Secrets masked until you hold <kbd>Space</kbd>, auto-lock with dimming, <kbd>G0</kbd> side button locks instantly |
| 📡 **Air-gapped** | The firmware never turns on Wi-Fi or Bluetooth |

## Install

**Web flasher (easiest):** open the CardVault web flasher (GitHub Pages of this repo) in Chrome/Edge,
put the Cardputer in download mode (power off, hold <kbd>G0</kbd>, plug USB-C) and click *Install*.

**esptool:** download `cardvault-vX.Y.Z-cardputer-adv.bin` from
[Releases](https://github.com/Besermenji/cardvault/releases), check its SHA-256, then:

```sh
esptool.py --chip esp32s3 write_flash 0x0 cardvault-vX.Y.Z-cardputer-adv.bin
```

**M5Burner / Launcher:** search for *CardVault* in M5Burner's Cardputer section, or install it from
[Launcher](https://github.com/bmorcelli/Launcher)'s OTA list.

**From source:**

```sh
pio run -e cardputer -t upload
```

## Using it

| Key | Action |
|---|---|
| <kbd>fn</kbd> + <kbd>;</kbd> <kbd>.</kbd> <kbd>,</kbd> <kbd>/</kbd> | Up / Down / Left / Right (plain <kbd>;</kbd> <kbd>.</kbd> work in lists) |
| <kbd>Enter</kbd> | Open / confirm |
| <kbd>Tab</kbd> | Menu / save |
| <kbd>fn</kbd> + <kbd>`</kbd> (or <kbd>`</kbd> in lists) | Back / cancel |
| <kbd>opt</kbd> | Show/hide the passphrase while typing |
| hold <kbd>Space</kbd> / <kbd>R</kbd> | Reveal the selected secret / toggle reveal all |
| <kbd>G0</kbd> (side button) | Lock immediately |

1. **First boot** — create a passphrase (twice). Use something long: five or more random words is ideal.
2. **Add secrets** — <kbd>Tab</kbd> → *New secret* → Seed phrase, Password, 2FA or Note.
3. **Seed phrases** — choose 12/18/24 words, type the first letters of each word and press
   <kbd>Enter</kbd>; CardVault checks the BIP39 checksum when you finish.
4. **2FA** — paste the *setup key* (base32) shown next to the QR code. Set the clock once per power-up
   (Settings → Set clock, UTC): the Cardputer ADV has no battery-backed clock.
5. **Backups** — Settings → *Backup to SD*. You choose a passphrase for the backup (it may be the same).
   Restore with Settings → *Restore from SD* (also available on the lock screen via <kbd>Tab</kbd>).

## Security model

**How your data is protected**

- A random 256-bit data key encrypts the vault with **XChaCha20-Poly1305**. The data key is wrapped by a
  key derived from your passphrase with **Argon2id** (memory-hard; parameters calibrated per device and
  stored in the file header). All header fields are authenticated; any modified byte is rejected.
- Plaintext is padded to 256-byte blocks so the file size leaks little about its contents.
- Decrypted data lives only in RAM while unlocked and is wiped (`crypto_wipe`) on lock, including
  every string buffer (wiping allocators).
- The failure counter is written to flash **before** each passphrase check, so pulling the battery
  doesn't reset it. After 10 consecutive failures the whole vault partition is erased.
- Randomness comes from the ESP32-S3 hardware RNG with its ADC entropy source enabled, mixed with
  keystroke timing through a ratcheting BLAKE2b pool.

**What it does NOT protect against**

- **Flash dumps.** Without ESP32 secure boot + flash encryption (irreversible eFuse changes, not enabled
  by default), an attacker holding your device can read the encrypted vault off the flash chip and guess
  passphrases on a computer, bypassing the 10-attempt limit. Only a **strong passphrase** stops that.
- A device that was tampered with (malicious firmware) *before* you type your passphrase.
- Shoulder surfing and cameras while secrets are revealed.
- Loss of the device without a backup.

See [SECURITY.md](SECURITY.md) for reporting vulnerabilities.

### Vault file format (v1)

```
header (104 B): "CVLT" | ver | kdf=2 | rsvd | mem_kib u32 | passes u32 | salt[16]
                wrap_nonce[24] | wrapped_dek[32] | wrap_mac[16]
body:           nonce[24] | mac[16] | XChaCha20-Poly1305(dek, u32 len | payload | pad)
```

The payload is a compact TLV list of entries (`src/vault/serializer.h`).

## Development

```sh
pio test -e native        # host unit tests: crypto vectors, BIP39, TOTP, vault format, guard
pio run -e cardputer      # firmware
tools/merge_bin.sh 0.1.0  # single image in release/
```

Layout: `src/crypto`, `src/vault`, `src/features`, `src/security` are Arduino-free and unit tested;
`src/platform` talks to the hardware; `src/ui` holds the screens. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE). Monocypher is vendored under its own BSD-2-Clause / CC0 license.
