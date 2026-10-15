#pragma once

#include <cstdint>

namespace usb_format {

enum class Result : uint8_t {
    none,
    running,
    ok,
    /// No USB drive is connected
    no_drive,
    /// A print, an upload or an open file uses the drive
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
/// (32 KiB once the partition is 2 GiB or more), and the data area aligned to 4 MiB. A GPT the drive had is
/// cleared too.
///
/// Refuses while an upload runs or any file is open on the drive, and other users of the drive wait until
/// it ends. Blocks for seconds to minutes, so don't call it from the Marlin thread, which has to keep feeding
/// the watchdog.
Result format_drive();

/// The result of the last M1706, for the GUI that ran it.
Result last_result();
void set_last_result(Result result);

} // namespace usb_format
