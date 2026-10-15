#pragma once

#include <cstdint>

namespace usb_format {

enum class Result : uint8_t {
    none,
    running,
    ok,
    /// No USB drive is connected
    no_drive,
    /// The drive reports no capacity, like a card reader without a card
    no_medium,
    /// A print, a file transfer or an open file uses the drive
    busy,
    /// The drive's sectors aren't 512 bytes
    unsupported_drive,
    /// Too small for FAT32
    too_small,
    out_of_memory,
    /// A write failed: the drive may be write-protected, faulty or removed
    write_failed,
    failed,
};

/// Erases the USB drive and formats it as FAT32 on an MBR partition, with two FATs, FatFs's cluster size
/// (32 KiB on drives of 4 GB and up), and the data area aligned to 4 MiB. A GPT the drive had is cleared too,
/// except for the backup copy at the end of a drive over 2 TiB, which the printer can't address.
///
/// Refuses while a file transfer runs or a file is open on the drive. Meanwhile, file access to the drive
/// fails with EBUSY instead of waiting. Blocks for seconds to minutes, so don't call it from the Marlin
/// thread, which has to keep feeding the watchdog.
Result format_drive();

/// The result of the last M1706, for the GUI that ran it.
Result last_result();
void set_last_result(Result result);

} // namespace usb_format
