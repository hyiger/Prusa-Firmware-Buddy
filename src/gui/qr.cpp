#include <gui/qr.hpp>

#include "window_qr.hpp"
#include <common/error_code_mangle.hpp>
#include <common/support_utils.h>
#include <find_error.hpp>
#include <version/version.hpp>

#include <string_view>

QRStaticStringWindow::QRStaticStringWindow(window_t *parent, Rect16 rect, Align_t align, const char *data)
    : window_aligned_t { parent, rect }
    , data { data } {
    SetAlignment(align);
}

void QRStaticStringWindow::unconditionalDraw() {
    draw_qr({
        .data = data,
        .rect = GetRect(),
        .align = GetAlignment(),
    });
}

QRErrorUrlWindow::QRErrorUrlWindow(window_t *parent, Rect16 rect, ErrCode ec)
    : QRErrorUrlWindow(parent, rect) {
    set_error_code(ec);
}

QRErrorUrlWindow::QRErrorUrlWindow(window_t *parent, Rect16 rect)
    : QRDynamicStringWindow { parent, rect, Align_t::Center() } {
}

void QRErrorUrlWindow::set_error_code(ErrCode ec) {
    static_assert(sizeof(buffer) >= sizeof("https://") + max_help_url_prefix_length + 5, "Room for the scheme and the five-digit code");
    const std::string_view url_prefix = find_error_help_url_prefix(ec);
    StringBuilder builder { buffer };
    builder.append_string("https://");
    builder.append_std_string_view(url_prefix);
    builder.append_printf("%05d", map_error_code(ec));
    // Only Prusa's knowledge base makes use of the printer identification
    if (url_prefix == prusa_help_url_prefix && config_store().devhash_in_qr.get()) {
        {
            char printer_code[10] = {};
            printerCode(printer_code);
            builder.append_string("?ref_data=");
            builder.append_string(printer_code);
        }
        {
            char version[10] = {};
            version::fill_project_version_no_dots(version, sizeof(version));
            builder.append_string("-");
            builder.append_string(version);
        }
    }
    Invalidate();
}
