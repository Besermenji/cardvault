# Security review — October 2026 (v0.1.0)

Self-review of the CardVault code base before making the repository public (issue #20).
Scope: everything under `src/`, the on-disk format and the release tooling.

## Findings

| # | Severity | Area | Finding | Status |
|---|---|---|---|---|
| 1 | **High** | Attempt guard | When the device was locked, a successful decrypt in the SD restore flow called `record_success()`, which reset the failure counter even if the user then declined to replace the vault. Someone could alternate 9 guesses at the device passphrase with a "restore" of their own backup (whose passphrase they know) and get unlimited guesses. | Fixed: a successful backup decrypt only refunds its own attempt; the counter is reset only after the vault has actually been replaced. |
| 2 | Low | Memory hygiene | The deserializer built entries and fields in stack temporaries and moved them into the vault. Moving a small-string-optimised `std::string` (< 16 chars) copies its bytes and leaves the originals on the stack. | Fixed: entries and fields are constructed in place inside wiping containers. |
| 3 | Info | Release | The release workflow's version check rejected prerelease tags such as `v0.1.0-rc1`. | Fixed in #18. |

## Checked, no issue found

- **Crypto construction.** Fresh random 24-byte nonces for every wrap and every save (XChaCha20, so random nonces are safe). The wrap AD covers the KDF parameters and salt; the body AD covers the whole header, so swapping headers between files or downgrading parameters is detected. Argon2id with a single lane; parameters bounded (64–4096 KiB, 3–1000 passes, and memory × passes ≤ 256000 KiB-passes, roughly 10 s) so a crafted header can only cause a refused unlock (refunded), not a crash.
- **Parsing.** Header decode checks length, magic, version and KDF bounds. The body length must be a multiple of the pad block and below the payload limit. The decrypted length prefix is bounds-checked. The TLV reader checks every read and rejects trailing bytes.
- **Attempt guard ordering.** The counter is committed to NVS before Argon2 runs. A power cut during the check counts as a failure, and an exhausted counter found at boot triggers the wipe. Each reboot restarts the waiting delay from the full value, so rebooting doesn't skip delays.
- **Wipe.** It erases the whole data partition, not just the file, then resets the counter.
- **Randomness.** The hardware RNG runs with its SAR ADC entropy source enabled (radios are off), mixed through a ratcheting BLAKE2b pool that is also stirred with keystroke timings. Modulo bias is avoided by rejection sampling.
- **Radios.** No Wi-Fi or Bluetooth initialisation exists in the code; BT controller memory is released at boot.
- **Logging.** `CORE_DEBUG_LEVEL=0`. The firmware never writes to `Serial`.

## Residual risks (accepted, documented)

- **Offline brute force after a flash dump.** Without flash encryption, the vault ciphertext can be read over USB download mode, and then the 10-attempt limit doesn't apply. Mitigation: a strong passphrase. See [HARDENING.md](HARDENING.md) for eFuse-based options.
- **Short-lived copies of secrets in RAM.** Temporary strings shorter than 16 characters live inside the string object (on the stack), where the wiping allocator can't reach them. The M5Cardputer library's key-state buffer also holds recently typed characters. These copies are overwritten quickly, live only in SRAM while the device is powered, and are lost at power-off. Extracting them needs code execution or JTAG on an *unlocked* device, which already exposes everything.
- **The screen frame buffer** holds whatever is displayed (including revealed secrets) until the next redraw. The lock screen redraws the full frame immediately.
- **USB-JTAG** is enabled on stock ESP32-S3 parts. Someone with a powered, *unlocked* device could read RAM. A locked device holds no decrypted data.
