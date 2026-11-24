#ifndef __USB_HOST__H__
#define __USB_HOST__H__

#ifdef __cplusplus

    #include <option/has_usb_drive_format.h>

namespace usb_host {

/// \returns whether there is a USB drive inserted in the USB slot
bool is_media_inserted();

/// \returns whether there is a USB drive connected since startup
bool is_media_inserted_since_startup();

// In case media is invalid, this will disable it until reconnection
void disable_media();

    #if HAS_USB_DRIVE_FORMAT()
/// \returns whether a drive is attached, be it one with a file system the printer cannot read
bool is_drive_attached();

/// Erases the attached drive: makes a single FAT32 volume of all of it.
///
/// Blocks until done, tens of seconds on a large drive. The access of other tasks to the drive fails
/// meanwhile and the files they have open on it are lost, so call it when nothing uses the drive.
/// \returns whether the drive was formatted and its new file system mounts
bool format_drive();
    #endif
} // namespace usb_host

namespace usbh_power_cycle {

void init();

// callback from USBH_MSC_Worker when an io error occurs
void io_error();

// callback from isr, it is called when the USB is disconnected or when USB flash is deadlocked
void port_disabled();

// indication that the one click dialog during USB recovery reset should be blocked
bool block_one_click_print();

} // namespace usbh_power_cycle

extern "C" {
#endif

#include "usbh_def.h"

void MX_USB_HOST_Init(void);

void USBH_UserProcess(USBH_HandleTypeDef *, uint8_t id);
extern TimerHandle_t USBH_restart_timer;

#ifdef __cplusplus
}
#endif

#endif /* __USB_HOST__H__ */
