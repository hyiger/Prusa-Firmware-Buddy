#!/usr/bin/env python3
"""Writes build/mini404/eeprom.bin for the emulated CORE One: first-run setup done, in English,
without the MMU and with the chamber vents left alone, which the emulator cannot move.
"""
import os
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RUN = Path(os.environ.get('MINI404_RUN', REPO / 'build' / 'mini404'))

sys.path.insert(0, str(REPO / 'utils'))
from persistent_stores import eeprom  # noqa: E402

ITEMS = {
    'Language': b'en',
    'Printer network done': struct.pack('<B', True),
    'Printer hw-config done': struct.pack('<B', True),
    # Without an MMU emulator, the firmware turns the MMU off anyway, and its print start hacks get in the way
    'MMU2 Enabled': struct.pack('<B', False),
    'Auto chamber vent enabled': struct.pack('<B', False),
    'Check chamber ventilation state': struct.pack('<B', False),
    'FSensor Enabled': struct.pack('<B', False),
    'Emergency stop disable consent': struct.pack('<B', True),
    'Run Selftest': struct.pack('<B', False),
    'Run XYZ Calibration': struct.pack('<B', False),
    'Run First Layer': struct.pack('<B', False),
}


def rbit(x):
    return int(f'{x:032b}'[::-1], 2)


def emulated_crc32(crc, data):
    """crc32_calc_ex() as it runs in Mini404.

    Whole words go through the STM32 CRC unit. Its emulation resets to 0 instead of 0xFFFFFFFF, so a
    calculation that starts from 0 differs from real hardware. The rest goes through crc32_sw().
    """
    words = len(data) // 4
    if words:
        dr = 0 if crc == 0 else rbit(crc ^ 0xFFFFFFFF)
        for i in range(words):
            dr ^= rbit(struct.unpack_from('<I', data, 4 * i)[0])
            for _ in range(32):
                carry = dr & 0x80000000
                dr = (dr << 1) & 0xFFFFFFFF
                if carry:
                    dr ^= 0x04C11DB7
        crc = rbit(dr) ^ 0xFFFFFFFF
    return eeprom.crc32_sw(data[4 * words:], crc)


def main():
    items = [(eeprom.generate_id(name), data) for name, data in ITEMS.items()]

    journal = b''
    crc = 0
    for i, (item_id, item_data) in enumerate(items):
        header = eeprom.generate_item_header(int(i == len(items) - 1), item_id,
                                             len(item_data))
        crc = emulated_crc32(emulated_crc32(crc, header), item_data)
        journal += header + item_data
    journal += struct.pack('<I', crc)

    bank_header = struct.pack('<IH', 1, 1)
    bank = bank_header + struct.pack('<I', emulated_crc32(
        0, bank_header)) + journal
    image = b'\xff' * eeprom.FIRST_BANK_OFFSET + bank.ljust(
        eeprom.BANK_SIZE, b'\xff')
    RUN.mkdir(parents=True, exist_ok=True)
    (RUN / 'eeprom.bin').write_bytes(image.ljust(8192, b'\xff'))


if __name__ == '__main__':
    main()
