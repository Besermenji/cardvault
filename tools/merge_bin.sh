#!/usr/bin/env bash
# Builds a single flashable image (bootloader + partitions + app) for
# esptool / web flasher / M5Burner. Run after `pio run -e <env>`.
#   usage: tools/merge_bin.sh <version> [out_dir] [env]
# env defaults to "cardputer", the only release environment. Its image is
# checked with tools/check_release_image.sh and deleted if the check fails.
# Any other env is written as ...-<env>-NOT-FOR-RELEASE.bin and not checked.
set -euo pipefail

VERSION="${1:?version required, e.g. 0.1.0}"
OUT="${2:-release}"
ENV_NAME="${3:-cardputer}"
BUILD=".pio/build/$ENV_NAME"
BOOT_APP0="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

if [ "$ENV_NAME" = "cardputer" ]; then
    # Flags from the shell would be built into a "release" without showing up in platformio.ini.
    if [ -n "${PLATFORMIO_BUILD_FLAGS:-}" ]; then
        echo "refusing a release image: PLATFORMIO_BUILD_FLAGS is set ($PLATFORMIO_BUILD_FLAGS)" >&2
        exit 1
    fi
    NAME="cardvault-v${VERSION}-cardputer-adv"
else
    NAME="cardvault-v${VERSION}-${ENV_NAME}-NOT-FOR-RELEASE"
fi

mkdir -p "$OUT"
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 merge_bin -o "$OUT/${NAME}.bin" \
    --flash_mode keep --flash_freq keep --flash_size keep \
    0x0 "$BUILD/bootloader.bin" \
    0x8000 "$BUILD/partitions.bin" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$BUILD/firmware.bin"

if [ "$ENV_NAME" != "cardputer" ]; then
    echo "Wrote $OUT/${NAME}.bin (not a release image)"
    exit 0
fi

if ! "$(dirname "$0")/check_release_image.sh" "$OUT/${NAME}.bin"; then
    rm -f "$OUT/${NAME}.bin"
    exit 1
fi

cp "$BUILD/firmware.bin" "$OUT/${NAME}-app-only.bin"
if command -v sha256sum >/dev/null; then SHA="sha256sum"; else SHA="shasum -a 256"; fi
(cd "$OUT" && $SHA "${NAME}.bin" "${NAME}-app-only.bin" > "${NAME}.sha256")
echo "Wrote $OUT/${NAME}.bin"
cat "$OUT/${NAME}.sha256"
