#include "PrusaGcodeSuite.hpp"

#include <Marlin.h>
#include <async_job/async_job.hpp>
#include <common/marlin_server.hpp>
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <marlin_vars.hpp>
#include <usb_host/usb_format.hpp>

namespace PrusaGcodeSuite {

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1706: Format the USB drive
 *
 * Internal GCode
 *
 * Erases everything on the USB drive and formats it as FAT32 on an MBR partition. Refused while a print, a
 * file transfer or an open file uses the drive.
 *
 *#### Usage
 *
 *    M1706
 */
void M1706() {
    if (marlin_vars().print_state.get() != marlin_server::State::Idle) {
        usb_format::set_last_result(usb_format::Result::busy);
        return;
    }

    usb_format::Result result;
    {
        // Shows a wait screen, and the printer reports itself busy, so no print starts meanwhile.
        marlin_server::FSM_Holder fsm_holder { PhaseWait::print_status_message };
        PrintStatusMessageGuard status_message;
        status_message.update<PrintStatusMessage::formatting_usb_drive>({});

        // The format takes seconds to minutes. Meanwhile this thread keeps idling, which feeds the watchdog.
        AsyncJobWithResult<usb_format::Result> job;
        job.issue([](AsyncJobExecutionControl &, usb_format::Result &job_result) {
            job_result = usb_format::format_drive();
        });
        while (job.is_active()) {
            idle(true);
        }
        result = job.result();
    }
    // Once the wait screen has closed, so that the GUI's message doesn't open under it.
    usb_format::set_last_result(result);
}

/** @}*/

} // namespace PrusaGcodeSuite
