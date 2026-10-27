# Publishing a release

1. Update `src/version.h` and move the `[Unreleased]` notes in `CHANGELOG.md` under the new version.
2. Commit, then tag: `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. The **Release** workflow tests, builds, merges the image, creates the GitHub release
   (merged image, app-only image, SHA-256 file) and, for public repos, deploys the web flasher to Pages.

## M5Burner

Publishing needs the maintainer's M5Stack community account, so it is done by hand:

1. Open M5Burner and log in (top-right).
2. **USER CUSTOM** (bottom-left) → **Publish**.
3. Fill in:
   - Name: `CardVault`
   - Version: `X.Y.Z`
   - Device: **Cardputer**
   - Description: short feature summary + "Wi-Fi/BT never used. Source: GitHub link" + the support
     line (Launcher shows the same text):
     `Support: BTC bc1qwa2d9rg06dz97rdcjg00cy3j9myevawq9075fy / ETH 0xa263474B98a69478aaeb7ac5562f1647D3c24712`
   - GitHub: `https://github.com/Besermenji/cardvault`
   - Firmware: `cardvault-vX.Y.Z-cardputer-adv.bin` (merged image, flashed at `0x0`)
   - Cover: a 240×135 screenshot of the home screen.
4. Submit and wait for the listing to appear.

## Launcher

[Launcher](https://github.com/bmorcelli/Launcher) lists firmware from the M5Burner database, so a
published M5Burner entry becomes installable over the air. Launcher reads the partition table inside
the merged image and creates CardVault's `vault` data partition by label. CardVault never falls back to
shared partitions (`spiffs`, `littlefs`, `fat`). Keep `firmware.bin` small (CI enforces a 2 MB budget).
