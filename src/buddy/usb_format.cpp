#include <usb_host/usb_format.hpp>

#include "usb_host.h"
#include <buddy/fatfs.h>
#include <diskio.h>
#include <ff.h>
#include <heap.h>
#include <logging/log.hpp>
#include <raii/scope_guard.hpp>
#include <transfers/changed_path.hpp>
#include <transfers/monitor.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

LOG_COMPONENT_REF(USBHost);

namespace usb_format {

namespace {

    std::atomic<Result> last_result_ { Result::none };

    constexpr BYTE drive = 0;

    /// A GPT header and its 32 sectors of partition entries, at the start of the drive and again at its end
    constexpr UINT gpt_sectors = 33;

    /// f_mkfs puts the partition at this sector and writes nothing before it but the MBR
    constexpr LBA_t partition_start = 63;

    /// Zeroes @p count sectors from @p first, @p buf_sectors at a time from the zeroed @p buf.
    bool zero_sectors(const BYTE *buf, UINT buf_sectors, LBA_t first, UINT count) {
        while (count > 0) {
            const UINT n = std::min(count, buf_sectors);
            if (disk_write(drive, buf, first, n) != RES_OK) {
                return false;
            }
            first += n;
            count -= n;
        }
        return true;
    }

    /// Takes the volume's lock and holds it until it returns.
    Result format_locked(BYTE *work, UINT len, LBA_t last_sector) {
        FATFS &fs = USBHFatFS;

        // f_mkfs doesn't take the volume's lock. Holding it keeps everything else waiting until the new file
        // system is in place.
        if (!ff_req_grant(fs.sobj)) {
            return Result::failed;
        }
        ScopeGuard unlock = [&fs] {
            ff_rel_grant(fs.sobj);
        };

        // An open file would go on writing into the new file system.
        if (const UINT open = ff_count_open_objects(&fs); open != 0) {
            log_warning(USBHost, "Not formatting: %u files open", open);
            return Result::busy;
        }

        // Two FATs, as Windows and mkfs.fat make them. Flash drives report no erase block size, so align the
        // data area to 4 MiB, a multiple of the usual ones.
        const MKFS_PARM parm {
            .fmt = FM_FAT32,
            .n_fat = 2,
            .align = 8192,
            .n_root = 0,
            .au_size = 0,
        };
        switch (const FRESULT res = f_mkfs(USBHPath, &parm, work, len)) {
        case FR_OK:
            break;
        case FR_DISK_ERR:
        case FR_NOT_READY:
        case FR_WRITE_PROTECTED:
            log_error(USBHost, "Format failed: %d", res);
            return Result::write_failed;
        case FR_MKFS_ABORTED:
            return Result::too_small;
        case FR_NOT_ENOUGH_CORE:
            return Result::out_of_memory;
        default:
            log_error(USBHost, "Format failed: %d", res);
            return Result::failed;
        }

        // f_mkfs writes the MBR and the volume only, so clear what's left of a GPT, which the printer can't read.
        // Done last, so that a drive f_mkfs refuses keeps its file system.
        uint64_t gpt_backup_header = 0;
        if (disk_read(drive, work, 1, 1) == RES_OK && memcmp(work, "EFI PART", 8) == 0) {
            memcpy(&gpt_backup_header, work + 32, sizeof(gpt_backup_header));
        }
        memset(work, 0, len);
        const UINT buf_sectors = len / FF_MAX_SS;
        if (!zero_sectors(work, buf_sectors, 1, partition_start - 1)) {
            return Result::write_failed;
        }
        // Only where the GPT puts its backup, at the last sector: some USB bridges report one sector more than
        // they have. The backup on a drive over 2 TiB is past the 0xFFFFFFFF sectors it reports.
        if (gpt_backup_header == last_sector
            && !zero_sectors(work, buf_sectors, last_sector - gpt_sectors + 1, gpt_sectors)) {
            return Result::write_failed;
        }
        return Result::ok;
    }

} // namespace

Result format_drive() {
    if (!usb_host::begin_format()) {
        return Result::no_drive;
    }
    ScopeGuard end_format = [] {
        usb_host::end_format();
    };

    // An upload writes to the drive until it ends.
    if (transfers::Monitor::instance.id().has_value()) {
        return Result::busy;
    }

    DWORD sector_size = 0;
    if (disk_ioctl(drive, GET_SECTOR_SIZE, &sector_size) != RES_OK) {
        return Result::failed;
    }
    // A card reader without a card, until it's reconnected with one
    if (sector_size == 0) {
        return Result::no_medium;
    }
    if (sector_size != FF_MAX_SS) {
        return Result::unsupported_drive;
    }
    // The driver reports the last sector, not the number of sectors.
    LBA_t last_sector = 0;
    if (disk_ioctl(drive, GET_SECTOR_COUNT, &last_sector) != RES_OK) {
        return Result::failed;
    }

    // DMA writes the buffer to the drive, so it can't be on a task stack, which lives in CCM RAM. A bigger
    // buffer means fewer and faster writes.
    BYTE *work = nullptr;
    UINT len = 0;
    for (const UINT size : { 16384u, 8192u, 4096u, 512u }) {
        if ((work = static_cast<BYTE *>(malloc_fallible(size))) != nullptr) {
            len = size;
            break;
        }
    }
    if (work == nullptr) {
        return Result::out_of_memory;
    }
    ScopeGuard free_work = [work] {
        free(work);
    };

    if (const Result result = format_locked(work, len, last_sector); result != Result::ok) {
        return result;
    }

    // Mounts the new file system, which also checks it.
    DWORD free_clusters = 0;
    FATFS *fs = nullptr;
    if (f_getfree(USBHPath, &free_clusters, &fs) != FR_OK) {
        log_error(USBHost, "Formatted drive doesn't mount");
        return Result::failed;
    }
    log_info(USBHost, "USB drive formatted, %u clusters of %u sectors", static_cast<unsigned>(free_clusters), static_cast<unsigned>(fs->csize));

    // A drive the printer refused for its file system is usable now.
    usb_host::enable_media();

    // Prusa Connect rescans the drive, and PrusaLink's listings of it change their ETags.
    using transfers::ChangedPath;
    ChangedPath::instance.changed_path("/usb/", ChangedPath::Type::Folder, ChangedPath::Incident::Combined);

    return Result::ok;
}

Result last_result() {
    return last_result_;
}

void set_last_result(Result result) {
    last_result_ = result;
}

} // namespace usb_format
