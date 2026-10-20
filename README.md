# CardVault

**An offline, encrypted secrets vault firmware for the M5Stack Cardputer ADV.**

Store BIP39 seed phrases, passwords, 2FA (TOTP) seeds and private notes on your Cardputer,
encrypted with a passphrase. No Wi-Fi. No Bluetooth. No cloud.

> ⚠️ Work in progress — not ready for real funds yet.

## Features (planned for v1.0)

- 🔐 Argon2id + XChaCha20-Poly1305 encryption (Monocypher)
- 🧠 BIP39 helper: word autocomplete, checksum validation, one-word-at-a-time reveal
- 🔑 Password generator using the ESP32 hardware RNG
- ⏱️ TOTP 2FA codes
- 💾 Encrypted backup / restore to SD card
- 🛡️ Escalating delays and auto-wipe after 10 wrong passphrases
- 🙈 Auto-lock and masked-by-default secrets

## Building

```sh
pio run -e cardputer -t upload
pio test -e native
```

## Security

Read [SECURITY.md](SECURITY.md) for the threat model before trusting CardVault with anything valuable.

## License

[MIT](LICENSE)
