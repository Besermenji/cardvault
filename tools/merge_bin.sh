#!/usr/bin/env bash
# Builds a single flashable image (bootloader + partitions + app) for
# esptool / web flasher / M5Burner. Run after `pio run -e cardputer`.
#   usage: tools/merge_bin.sh <version> [out_dir]
set -euo pipefail

VERSION="${1:?version required, e.g. 0.1.0}"
OUT="${2:-release}"
BUILD=".pio/build/cardputer"
BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

mkdir -p "$OUT"
NAME="cardvault-v${VERSION}-cardputer-adv"

pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin -o "$OUT/${NAME}.bin" \
    --flash_mode keep --flash_freq keep --flash_size keep \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"

cp "$BUILD/firmware.bin" "$OUT/${NAME}-app-only.bin"
if command -v sha256sum >/dev/null; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
(cd "$OUT" && $SHA "${NAME}.bin" "${NAME}-app-only.bin" > "${NAME}.sha256")
echo "Wrote $OUT/${NAME}.bin"
cat "$OUT/${NAME}.sha256"
