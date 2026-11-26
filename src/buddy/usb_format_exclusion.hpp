#pragma once

#include <atomic>
#include <cstdint>

/// Keeps formatting the USB drive and starting a print from it apart.
///
/// There is no lock for either side to hold while it waits for the other: a print start waits for
/// an asynchronous job, and formatting runs as one. Each side announces itself before it looks for
/// the other, so of two that overlap, at least one sees the other and gives up.
class UsbFormatExclusion {
public:
    /// Announces a format, then asks \p is_drive_in_use whether a print or a transfer uses the drive.
    /// \returns whether the format may go ahead, and end_format must follow it then. Refused while
    /// another format runs, a print start is announced, or the drive is in use.
    template <class IsDriveInUse>
    bool begin_format(IsDriveInUse &&is_drive_in_use) {
        if (formatting_.exchange(true)) {
            return false;
        }
        if (print_starts_ > 0 || is_drive_in_use()) {
            formatting_ = false;
            return false;
        }
        return true;
    }

    void end_format() {
        formatting_ = false;
    }

    /// \returns whether a format is on. A transfer checks it under the lock that the formatter's
    /// is_drive_in_use reads the transfer under.
    bool is_formatting() const {
        return formatting_;
    }

    /// Announces a print start. \returns false while a format is on, and the print must not start.
    /// Otherwise end_print_start must follow, once is_drive_in_use sees the print, or once the start
    /// is given up.
    bool begin_print_start() {
        ++print_starts_;
        if (formatting_) {
            --print_starts_;
            return false;
        }
        return true;
    }

    void end_print_start() {
        --print_starts_;
    }

private:
    std::atomic<bool> formatting_ = false;
    std::atomic<uint8_t> print_starts_ = 0;
};
