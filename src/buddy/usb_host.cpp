#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_msc.h"

#include <buddy/fatfs.h>
#include <device/board.h>
#include "common/timing.h"

#include <atomic>
#include "marlin_client.hpp"
#include <bsod/bsod.h>
#include <option/has_usb_drive_format.h>

#if HAS_USB_DRIVE_FORMAT()
    #include "usbh_async_diskio.hpp"
    #include <heap.h>
    #include <logging/log.hpp>
    #include <raii/scope_guard.hpp>
    #include <algorithm>
    #include <cstdlib>
    #include <cstring>

LOG_COMPONENT_REF(USBHost);
#endif

USBH_HandleTypeDef hUsbHostHS;

static std::atomic<bool> media_inserted = false;
static std::atomic<bool> media_inserted_since_startup = false;
static uint32_t media_on_startup_detection_timeout = 0;

namespace usb_host {

bool is_media_inserted() {
    return media_inserted;
}

bool is_media_inserted_since_startup() {
    return media_inserted_since_startup;
}

void disable_media() {
    media_inserted = false;
}

#if HAS_USB_DRIVE_FORMAT()
namespace {

    /// Mounts the volume if it is not mounted.
    FRESULT open_root_directory() {
        DIR directory;
        const FRESULT result = f_opendir(&directory, USBHPath);
        if (result == FR_OK) {
            f_closedir(&directory);
        }
        return result;
    }

    constexpr MKFS_PARM mkfs_parameters {
        .fmt = FM_FAT32,
        .n_fat = 2,
        // 4 MiB, as on SD cards: flash memory writes clusters that start on its own block boundaries faster
        .align = 8192,
        .n_root = 0,
        .au_size = 0,
    };

    /// [sectors, as the drive reports them to f_mkfs] FAT32 takes more than 65525 clusters, of a sector
    /// at the least, and its data area starts at the alignment
    constexpr DWORD min_drive_sectors = mkfs_parameters.align + 65526;

} // namespace

bool is_drive_attached() {
    return !(disk_status(0) & STA_NOINIT);
}

FormatResult format_drive() {
    if (!is_drive_attached()) {
        return FormatResult::failed;
    }

    // f_mkfs turns down a drive too small for FAT32, but only once the old partition tables are gone
    DWORD drive_sectors = 0;
    if (disk_ioctl(0, GET_SECTOR_COUNT, &drive_sectors) != RES_OK) {
        log_error(USBHost, "Formatting the drive failed: cannot read its size");
        return FormatResult::failed;
    }
    if (drive_sectors < min_drive_sectors) {
        log_error(USBHost, "The drive is too small for FAT32: %lu sectors", static_cast<unsigned long>(drive_sectors));
        return FormatResult::too_small;
    }

    // The file allocation table, megabytes of it on a large drive, is written a buffer at a time.
    size_t work_size = 16 * FF_MAX_SS;
    void *work = nullptr;
    while (!(work = malloc_fallible(work_size)) && work_size > FF_MAX_SS) {
        work_size /= 2;
    }
    if (!work) {
        log_error(USBHost, "No memory to format the drive");
        return FormatResult::failed;
    }
    ScopeGuard free_work = [&] { free(work); };

    usbh_msc_claim_drive();
    ScopeGuard release_drive = [] { usbh_msc_release_drive(); };

    // A file system call another task was in the middle of may still write. It holds the volume
    // lock until it is over, and any call of ours waits for that lock.
    (void)open_root_directory();

    // f_mkfs writes the partition table last. So that a format that does not get that far leaves no
    // file system behind, and not the previous one with a part of it overwritten, the old partition
    // tables go first: all the sectors ahead of the new volume, which hold the MBR and the primary GPT.
    // A backup GPT at the end of the drive stays. Without a protective MBR, computers ignore it.
    static constexpr LBA_t volume_start = 63;
    const UINT work_sectors = work_size / FF_MAX_SS;
    memset(work, 0, work_size);
    for (LBA_t sector = 0; sector < volume_start;) {
        const UINT count = std::min<LBA_t>(work_sectors, volume_start - sector);
        if (disk_write(0, static_cast<const BYTE *>(work), sector, count) != RES_OK) {
            log_error(USBHost, "Formatting the drive failed: cannot write to it");
            return FormatResult::failed;
        }
        sector += count;
    }

    FRESULT result = f_mkfs(USBHPath, &mkfs_parameters, work, work_size);
    if (result == FR_OK) {
        result = open_root_directory();
    }
    if (result != FR_OK) {
        log_error(USBHost, "Formatting the drive failed: %d", static_cast<int>(result));
        return FormatResult::failed;
    }

    // The drive may have been disabled for a file system the printer could not read
    media_inserted = true;
    return FormatResult::formatted;
}
#endif

} // namespace usb_host

namespace usbh_power_cycle {
// USB communication problems may occur at the physical layer. (emc interference, etc.)
// In the CPU, this will cause the HAL_HCD_Port Disabled callback or timeout in the io operation in the USBH_MSC_WorkerTask

// Functions in this namespace try to work around this by reinitializing the usbh stack (including turning off power on the bus)
// The procedure is 10ms pause => deinit usb => 150ms pause => init usb

// if printing was in progress:
//   if the usb is successfully initialized within 5s, it will connect automatically,
//   otherwise the "USB drive or file error" warning will appear.
// network transfers should continue (it's actually a new flash insertion)
// other operations may fail (highly unlikely situation)

// timer to handle the restart procedure
TimerHandle_t restart_timer;
void restart_timer_callback(TimerHandle_t);

uint32_t block_one_click_print_until_ms = 0;

enum class RecoveryPhase : uint_fast8_t {
    idle,

    /// USB has been turned off and will be turned back on at the end of this phase.
    restarting_usb,

};

std::atomic<RecoveryPhase> recovery_phase = RecoveryPhase::idle;

// Initialize FreeRTOS timer
void init() {
    static StaticTimer_t restart_timer_buffer;
    restart_timer = xTimerCreateStatic("USBHRestart", 10, pdFALSE, 0, restart_timer_callback, &restart_timer_buffer);
}

// callback from USBH_MSC_Worker when an io error occurs => start the restart procedure
void io_error() {
    if (recovery_phase == RecoveryPhase::idle) {
        xTimerChangePeriod(restart_timer, 10, portMAX_DELAY);
    }
}

// callback from isr => start the restart procedure
void port_disabled() {
    if (recovery_phase == RecoveryPhase::idle) {
        xTimerChangePeriodFromISR(restart_timer, 10, nullptr);
    }
}

// called from SVC task
void restart_timer_callback(TimerHandle_t) {
    switch (recovery_phase) {

    case RecoveryPhase::idle: {
        // If the phase is idle and the timer was called -> problem occured, start recovery process
        // This can either mean that the USB was disconnected,
        // or the communication had a problem (but the flash is still inserted and we need to recover)

        // Turn the USB off
        recovery_phase = RecoveryPhase::restarting_usb;
        USBH_Stop(&hUsbHostHS);

        // Call this timer again in 150 ms for the next phase
        xTimerChangePeriod(restart_timer, 150, portMAX_DELAY);
        break;
    }

    case RecoveryPhase::restarting_usb:
        // Prevent one click print from popping up when the drive initializes.
        // Most USBs should initialize within a few hundreds of ms.
        // Some drives take ~3 s to reinitialize, but we don't want to block the OCP for that long.
        // If user disconnects and reconnects the drive, he can do it faster than that and we want the OCP to trigger.
        block_one_click_print_until_ms = ticks_ms() + 800;

        // Reinitialize the USB host low-level driver
        // This seems to fix BFW-5333
        USBH_LL_DeInit(&hUsbHostHS);
        USBH_LL_Init(&hUsbHostHS);

        USBH_Start(&hUsbHostHS);

        // We're done here
        recovery_phase = RecoveryPhase::idle;
        xTimerStop(restart_timer, portMAX_DELAY);

        break;
    }
}

bool block_one_click_print() {
    return block_one_click_print_until_ms > ticks_ms();
}

} // namespace usbh_power_cycle

void MX_USB_HOST_Init(void) {
#if (BOARD_IS_XBUDDY() || BOARD_IS_XLBUDDY())
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_SET);
    osDelay(200);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_8, GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_SET);
    osDelay(200);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, GPIO_PIN_RESET);
#endif
    // A delay of 3000ms for detecting USB device (flash drive) was present at start
    media_on_startup_detection_timeout = ticks_ms() + 3000;

    usbh_power_cycle::init();
    if (USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS) != USBH_OK) {
        bsod_system();
    }
    if (USBH_RegisterClass(&hUsbHostHS, USBH_MSC_CLASS) != USBH_OK) {
        bsod_system();
    }
    if (USBH_Start(&hUsbHostHS) != USBH_OK) {
        bsod_system();
    }
}

void USBH_UserProcess([[maybe_unused]] USBH_HandleTypeDef *phost, uint8_t id) {
    // don't detect device at startup when ticks_ms() overflows (every ~50 days)
    if (media_on_startup_detection_timeout != 0 && ticks_ms() >= media_on_startup_detection_timeout) {
        media_on_startup_detection_timeout = 0;
    }

    switch (id) {

    case HOST_USER_DISCONNECTION:
        media_inserted = false;
        media_inserted_since_startup = false;
        f_mount(0, (TCHAR const *)USBHPath, 1); // umount
        break;

    case HOST_USER_CLASS_ACTIVE: {
        FRESULT result = f_mount(&USBHFatFS, (TCHAR const *)USBHPath, 0);
        if (result != FR_OK) {
            break;
        }

        media_inserted_since_startup = (media_on_startup_detection_timeout != 0);
        media_inserted = true;

        // Do not initialize marlin_client here - it blocks and media error
        // recovery doesn't even need it.
        marlin_client::try_recover_from_media_error();
        break;
    }

    default:
        break;
    }
}
