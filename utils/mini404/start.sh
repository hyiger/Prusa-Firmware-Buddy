#!/bin/sh
# Starts the emulated CORE One staged by stage.sh: the main board and the xBuddy extension, in one
# container. Ports are on localhost only: VNC on 5900, the script console on 7070, USB serial
# output on 7071 and PrusaLink on 8080.
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RUN="${MINI404_RUN:-$REPO/build/mini404}"
NAME=mini404-coreone

# A graceful stop lets QEMU write back the EEPROM
docker stop -t 20 "$NAME" >/dev/null 2>&1
docker rm -f "$NAME" >/dev/null 2>&1

docker run -d --name "$NAME" --platform linux/amd64 -v "$RUN":/work \
    -p 127.0.0.1:5900:5900 -p 127.0.0.1:7070:7070 -p 127.0.0.1:7071:7071 -p 127.0.0.1:8080:8080 \
    mini404:dev \
    -machine prusa-core-one -kernel /work/firmware.bin \
    -global STM32F4xx-usb.disable_sof_interrupt=true -icount 2 \
    -chardev socket,id=p404-scriptcon,port=7070,host=0.0.0.0,server=on,wait=off \
    -global p404-scriptcon.no_echo=true \
    -drive id=usbstick,format=raw,file=fat:rw:/work/usb -device usb-storage,drive=usbstick \
    -drive if=pflash,format=raw,file=/work/eeprom.bin \
    -drive if=mtd,format=raw,file=/work/xflash.bin \
    -chardev socket,id=stm32usbfscdc,port=7071,host=0.0.0.0,server=on,wait=off \
    -netdev user,id=mini-eth,hostfwd=tcp::8080-:80 \
    -vnc 0.0.0.0:0 >/dev/null || exit 1

# The main board waits on /tmp/PC1_EXT until the extension connects
sleep 2
docker exec -d "$NAME" sh -c \
    '/opt/mini404/qemu-system-buddy -machine prusa-xbuddy-extension-05 -kernel /work/xbe.bin -icount 4 -display none > /work/xbe.log 2>&1' \
    || exit 1
