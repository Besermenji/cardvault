# Updating CardVault

Updating doesn't change your vault, and you don't need to set anything up again. Make an SD backup
first anyway (Settings > *Backup to SD*), as with any firmware change.

There are four ways to update. Pick the one that matches how you installed CardVault.

| You installed with | Update with |
|---|---|
| The web flasher, esptool or M5Burner | The same tool again, or **the SD card** (from v0.4.0 on, see below) |
| Launcher | Launcher. SD updates are switched off under Launcher. |
| From source | `pio run -e cardputer -t upload`, or the SD card |

## Coming from v0.3.0 or older: one full install first

Updating from the SD card needs a second firmware slot on the device. Versions up to v0.3.0 don't
have one. Install the new version **once** with the web flasher, esptool or M5Burner. After that,
every later version can come from the SD card.

Your vault is kept: it sits in the same place in flash, and the full image stops before it. In the
web flasher, **leave "Erase device" unticked**. Erasing the device erases the vault.

If you try an SD update before this, the device says *Full install needed*.

## Update from the SD card

You need a microSD card formatted as FAT32 (the one you use for backups works).

1. Open the [latest release](https://github.com/Besermenji/cardvault/releases/latest) and download
   `cardvault-vX.Y.Z-cardputer-adv.cvu`. Take it from that page only. CardVault refuses any `.cvu`
   file that isn't signed with the CardVault release key, so a file from somewhere else won't install.
2. Optional: check its SHA-256 against the `.sha256` file next to it on the release page.
3. Copy the `.cvu` file into the `cardvault` folder on the card, the folder where your backups are.
   Create the folder if it isn't there.
4. Put the card in the Cardputer, unlock CardVault and open Settings > **Update firmware**.
5. Pick the version. The list shows every `.cvu` on the card. *installed* marks the version you run
   now, *older* marks an older one, and *not valid* marks a file that can't be installed (pick it to
   see why).
6. Read the question and press <kbd>Y</kbd>. A progress bar shows the install. It takes a few seconds.
   Keep the power on.
7. CardVault restarts and says *Updated: CardVault vX.Y.Z is installed*. Press <kbd>Enter</kbd> and
   unlock as usual.

Settings > *About* shows the version and the slot it runs from (`app0` or `app1`). The slot changes
with each SD update; that is normal.

You can delete the `.cvu` file from the card afterwards. It holds no secrets.

### If the power goes off

The new version is written next to the one you run. The device switches over only once the whole
file has been written and checked. If the power goes off during the install, the old version starts
as before and you can try again.

If the new version doesn't start properly, the device goes back to the old one by itself. It then
says *Update rolled back*. Your vault is not changed. Please report it in an
[issue](https://github.com/Besermenji/cardvault/issues).

### Older versions

The SD card only installs the same or a newer version. To go back to an older one, install it with
the web flasher or esptool (make a backup first: an older version may not read a vault written by a
newer one).

## Messages and what to do

| Message | Why | What to do |
|---|---|---|
| No SD card | No card, or it can't be read | Insert a FAT32 card and try again. |
| No update file | No `.cvu` in `/cardvault` | Copy the `.cvu` from the release into the `cardvault` folder. |
| Not available | CardVault runs under Launcher or with another partition table | Update CardVault from Launcher. |
| Full install needed | The device still has the partition table of v0.3.0 or older | Install once with the web flasher, esptool or M5Burner (see above). |
| Not an official CardVault file | Not signed with the CardVault release key | Download the file again from the GitHub release page only. |
| This update is for another device | Signed, but for different hardware | Download the `cardputer-adv` file. |
| Older version | The file is older than the version you run | Use the web flasher to go back, or pick a newer file. |
| The file is incomplete / damaged | Copying stopped, or the file changed | Copy it to the card again (check its SHA-256). |
| The card could not be read | The card failed or was pulled during the install | Check the card and try again. Nothing was changed. |
| Writing the update failed | The flash write failed | Nothing was changed. Try again; if it keeps failing, report it. |
| Your vault is only in memory | An earlier *Compact vault* could not write the vault back | Fix that first (Settings > *Compact vault*), then update. |

## Why the file is signed

Without a check, anyone holding your unlocked Cardputer could install firmware from a card, and a
lookalike firmware could record your passphrase. So CardVault installs a `.cvu` only if it is signed with the CardVault
release key, and only while the vault is unlocked. The signing key exists only in the release
workflow of this repository and in an offline backup (see [SECURITY.md](../SECURITY.md)).

On a device with secure boot and flash encryption (see [HARDENING.md](HARDENING.md)), USB flashing
is closed. There, the SD card would be the only way to update. That setup isn't supported yet
([#23](https://github.com/Besermenji/cardvault/issues/23)).
