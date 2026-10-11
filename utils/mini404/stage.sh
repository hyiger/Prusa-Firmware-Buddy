#!/bin/sh
# Builds a CORE One firmware for Mini404 and stages it in build/mini404, with the xBuddy extension
# firmware, a blank external flash and an EEPROM that skips the first-run setup.
#
# Extra arguments go to the main firmware build, to force options on:
#   utils/mini404/stage.sh -DHAS_FILAMENT_SLOTS:BOOL=YES
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RUN="${MINI404_RUN:-$REPO/build/mini404}"
cd "$REPO" || exit 1

# The extension runs its application without a bootloader, so the main board must not flash it
.venv/bin/python utils/build.py --preset coreone --build-type release --bootloader no \
    --skip-bootstrap -DENABLE_PUPPY_BOOTLOAD:BOOL=NO "$@" || exit 1
.venv/bin/python utils/build.py --preset coreone-xbuddy_extension --build-type release \
    --bootloader no --skip-bootstrap || exit 1

mkdir -p "$RUN/usb" "$RUN/shots" || exit 1
cp build/coreone_release_noboot/firmware.bin "$RUN/firmware.bin" || exit 1
# On first boot, the firmware installs its resources to the external flash from the .bbf
cp build/coreone_release_noboot/firmware.bbf "$RUN/usb/firmware.bbf" || exit 1
cp build/coreone-xbuddy_extension_release_noboot/firmware.bin "$RUN/xbe.bin" || exit 1

if [ ! -f "$RUN/xflash.bin" ]; then
    dd if=/dev/zero of="$RUN/xflash.bin" bs=1048576 count=8 2>/dev/null || exit 1
fi
if [ ! -f "$RUN/eeprom.bin" ]; then
    .venv/bin/python utils/mini404/mkeeprom.py || exit 1
fi
