# Hardening notes (research — not a procedure)

> ⚠️ **Do not burn eFuses based on this page.** eFuse changes are permanent. A mistake can make your
> Cardputer impossible to reflash, or erase your vault. This page records the analysis behind
> issue #23. A tested, step-by-step procedure will replace it once it has been verified on a spare
> device.

## The gap this would close

Stock CardVault encrypts the vault with your passphrase (Argon2id + XChaCha20-Poly1305) and erases it
after 10 wrong attempts **on the device**. Anyone holding the Cardputer can still put it in USB
download mode, read the flash with `esptool.py read_flash` and guess passphrases on a computer,
where the attempt limit doesn't apply. Today only a strong passphrase stops that.

The goal of hardening: **the encrypted vault can't be copied off the device**, so every guess has to go
through the on-device attempt guard.

## Relevant ESP32-S3 features

| Feature | What it does | Notes |
|---|---|---|
| Flash encryption, **release mode** | Flash contents are AES-XTS encrypted with a key held only in eFuse. Download mode can't write plaintext firmware, and reads return ciphertext. JTAG is disabled. | App partitions are always encrypted; **data partitions only if flagged `encrypted`** in the partition table, so the `vault` partition must get the flag. Updates then need the key or OTA. |
| Flash encryption, development mode | Same encryption, but plaintext flashing is still allowed. | Not protective: malicious firmware could be flashed and would read the vault. |
| Secure Download Mode | Download mode only accepts a minimal command set (no flash/memory reads). | Useful defence in depth together with release-mode encryption. |
| Secure Boot v2 | Bootloader and app must be signed with your key. | Stops someone replacing CardVault with a passphrase-logging lookalike ("evil maid"). Needs key management for every release. |
| Disable USB-JTAG / pad JTAG | Blocks debugger access to RAM and CPU. | Done automatically in release-mode flash encryption. |

The Cardputer ADV uses the Stamp-S3A module with an **ESP32-S3FN8**. The flash is very likely inside
the chip package (the "F" in the part number), which makes desoldering the flash much harder. Still
to verify.

## Open questions before this can become a procedure

1. **Toolchain.** The Arduino framework ships a precompiled bootloader without flash encryption. We
   need a bootloader built with `CONFIG_SECURE_FLASH_ENC_ENABLED` (and optionally secure boot), via
   ESP-IDF or a PlatformIO/pioarduino build with a custom sdkconfig. This must be tested.
2. **LittleFS on an encrypted partition.** Encrypted partitions have 16-byte write alignment and
   transparent decryption only through `esp_partition_*` reads. Need to check whether `esp_littlefs`
   works on an `encrypted` data partition, or store the vault blob directly with `esp_partition_write`
   (a simple A/B slot scheme would keep saves atomic).
3. **NVS.** The attempt counter lives in NVS. NVS encryption (or keeping the counter in an encrypted
   data partition) stops it being reset by rewriting flash. With release-mode encryption, plaintext
   writes are blocked anyway, but this needs confirming.
4. **Updates.** After release mode, USB flashing of plaintext images stops working. CardVault would
   need a signed update path (for example from the SD card) or users accept "no updates without
   re-provisioning".
5. **Launcher / M5Burner compatibility.** Hardened devices can't run other firmware. This should be
   an explicit, separate "hardened edition" install, never the default.

## Recommendation for now

- Use a **long random passphrase**. Six words from the built-in generator is 66 bits. Even an attacker
  computing a million Argon2id guesses per second would need over a million years on average.
  Human-chosen passphrases are far weaker than their length suggests.
- Keep the Cardputer physically secure, and keep **independent paper/metal backups** of seed phrases.
- Watch issue #23 for a tested procedure.

References: [ESP-IDF flash encryption (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/security/flash-encryption.html),
[Cardputer ADV docs](https://docs.m5stack.com/en/core/Cardputer-Adv).
