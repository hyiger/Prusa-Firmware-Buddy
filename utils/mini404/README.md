# CORE One in Mini404

Runs a CORE One firmware build in [Mini404](https://github.com/vintagepc/MINI404), vintagepc's QEMU
fork for Prusa's Buddy boards, inside Docker. It is for checking screens, settings and G-code
without flashing a printer.

Mini404 does not emulate the INDX, so the CORE One build stands in for it, with the INDX features
under test forced on. Code that only the INDX runs (tool changes, docks, tool detection) still
needs the printer.

## Running it

Docker Desktop must be running. From the repository root:

```sh
docker build --platform linux/amd64 -t mini404:dev utils/mini404
utils/mini404/stage.sh -DHAS_FILAMENT_SLOTS:BOOL=YES
utils/mini404/start.sh
```

`stage.sh` builds the firmware and the xBuddy extension firmware, and puts them in
`build/mini404` with a blank external flash and an EEPROM made by `mkeeprom.py`. The EEPROM skips
the first-run setup. Delete `build/mini404/eeprom.bin` to start from it again.

The first boot installs the firmware's resources from the `.bbf` on the emulated USB drive, which
takes a few minutes. Watch with Screen Sharing at `vnc://localhost:5900`. Stop with
`docker stop mini404-coreone`, which lets QEMU write back the EEPROM; `docker rm -f` loses it.

`p404.py` turns and presses the knob and saves screenshots to `build/mini404/shots`:

```sh
utils/mini404/p404.py seq 'twist -2' 'wait 5' 'push' 'wait 3' 'shot filament'
```

PrusaLink answers on `http://localhost:8080`, as user `maker` with the password shown under
Settings > Network > PrusaLink.

## Limits

- The firmware runs several times slower than real time, and reads the knob in its own time. A turn
  can land after the next command, so wait a few seconds after turning, and check the focus with a
  screenshot before pressing.
- Touch does not work, from VNC or otherwise. The emulated GT911 drops a touch as soon as the
  firmware clears its status register, so every touch looks about 10 ms long, and the firmware
  ignores it as electrical noise.
- G-code cannot be sent over the emulated USB serial port, which only carries the firmware's log.
  Put a `.gcode` file in `build/mini404/usb` and print it. The USB drive is read when the emulator
  starts, so restart after adding files.
- There is no MMU: Mini404 needs MK404 running the MMU firmware for that, and without it the
  firmware turns the MMU off. The emulator therefore has one tool.
- Moving the chamber vents hangs the emulated printer until the watchdog resets it, so the EEPROM
  turns the automatic vents off.
- The OpenPrintTag reader is not emulated, so the log repeats its Modbus errors.
- Mini404's CRC unit resets to 0 instead of 0xFFFFFFFF. `mkeeprom.py` follows the emulator, so
  its EEPROM images do not work on a printer, and the generator in `utils/persistent_stores` makes
  images this emulator rejects.
