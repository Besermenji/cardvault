# Publishing a release

1. Update `src/version.h` and move the `[Unreleased]` notes in `CHANGELOG.md` under the new version
   (`## [X.Y.Z]`). A `-rcN` tag uses `## [X.Y.Z-rcN]` if there is one, otherwise `[Unreleased]`, never the
   section of an earlier release. The release fails when there are no notes.
2. Merge to `main` and wait for CI to pass, then tag that exact commit (CI must have built it): `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. The **Release** workflow checks that the tag is on `main`, that CI passed for the commit and that the
   version and changelog match. In parallel it builds with read-only access and checks the merged image
   (`tools/check_release_image.sh`) and the flasher (`tools/check_flasher.sh`); a separate job, which needs
   both, creates the GitHub release (merged image, app-only image, SD update file `.cvu`, SHA-256 file). Once
   the repo is public the images also get a build attestation, and a final (non-`-rc`) release deploys the web
   flasher to Pages.
4. The `sign` job waits for approval (the `release` environment's required reviewer) and then signs the
   `.cvu` with the release key. Approve it in the run's page.

## SD update signing key

SD updates (#85, [UPDATING.md](UPDATING.md)) install only `.cvu` files signed with the release key
(Ed25519). Its public half is `tools/update_key.pub.pem` and the bytes in `src/platform/update_key.h`
(CI checks they match and that the key is not the test key or the placeholder).

- The private key is the secret `CV_UPDATE_KEY` of the GitHub environment `release`, which has the
  maintainer as required reviewer, plus one offline backup (paper or an encrypted USB stick, never the
  repository or a cloud drive). Only the `sign` job of `release.yml` reads it; that job installs nothing.
- Created once with OpenSSL 3:
  `openssl genpkey -algorithm ed25519 -out cardvault-update.pem`, then
  `gh api -X PUT repos/Besermenji/cardvault/environments/release` (creates the environment; add the
  reviewer under Settings > Environments), `gh secret set CV_UPDATE_KEY --env release < cardvault-update.pem`
  and `openssl pkey -in cardvault-update.pem -pubout -out tools/update_key.pub.pem` (commit the public key
  and copy its 32 bytes into `update_key.h`).
- **Key lost:** devices can no longer take SD updates from new releases. Make a new key as above; every
  user then installs one release with the web flasher, esptool or M5Burner, and SD updates work again.
- **Key leaked:** treat it as a security incident. Delete the secret, make a new key, publish a release
  with the new key (users install it over USB) and say in the release notes why.
- The `.cvu` version is the tag's (`v0.4.0-rc1` gives `0.4.0-rc1`). A device refuses older versions and
  accepts the same version again.

Verify a download with `sha256sum -c <name>.sha256`, and, for a public repo, with
`gh attestation verify <name>.bin --repo Besermenji/cardvault`.

Never tag from a local build: `tools/merge_bin.sh` refuses to write a release image if
`PLATFORMIO_BUILD_FLAGS` is set or the image check fails.

## M5Burner

Publishing needs the maintainer's M5Stack community account, so it is done by hand:

1. Open M5Burner and log in (top-right).
2. **USER CUSTOM** (bottom-left) → **Publish**.
3. Fill in:
   - Name: `CardVault`
   - Version: `X.Y.Z`
   - Device: **Cardputer**
   - Description: short feature summary + "Wi-Fi/BT never turned on. Source: GitHub link" + the support
     line (Launcher shows the same text):
     `Support: BTC bc1qwa2d9rg06dz97rdcjg00cy3j9myevawq9075fy / ETH 0xa263474B98a69478aaeb7ac5562f1647D3c24712`
   - GitHub: `https://github.com/Besermenji/cardvault`
   - Firmware: `cardvault-vX.Y.Z-cardputer-adv.bin` (merged image, flashed at `0x0`)
   - Cover: `docs/screenshots/cover.png` (240×135, the boot splash). `python3 tools/screenshots.py <port> --yes`
     regenerates it.
4. Submit and wait for the listing to appear.

## Launcher

[Launcher](https://github.com/bmorcelli/Launcher) lists firmware from the M5Burner database, so a
published M5Burner entry becomes installable over the air. Launcher reads the partition table inside
the merged image and creates CardVault's `vault` data partition by label. CardVault never falls back to
shared partitions (`spiffs`, `littlefs`, `fat`). Keep `firmware.bin` small (CI enforces a 2 MB budget).
