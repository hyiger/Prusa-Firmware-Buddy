#include <gui/text_error_url.hpp>

#include <common/error_code_mangle.hpp>
#include <find_error.hpp>
#include <utils/string_builder.hpp>

#include <algorithm>
#include <string_view>

TextErrorUrlWindow::TextErrorUrlWindow(window_t *parent, Rect16 rect, ErrCode ec)
    : TextErrorUrlWindow(parent, rect) {
    set_error_code(ec);
}

TextErrorUrlWindow::TextErrorUrlWindow(window_t *parent, Rect16 rect)
    : window_text_t { parent, rect, is_multiline::no } {
}

void TextErrorUrlWindow::set_error_code(ErrCode ec) {
    static_assert(sizeof(buffer) >= max_help_url_prefix_length + 7, "Room for the five-digit code, a line break and the terminating null");
    StringBuilder { buffer }
        .append_std_string_view(find_error_help_url_prefix(ec))
        .append_printf("%05u", map_error_code(ec));
    set_is_multiline(false);
    SetText(string_view_utf8::MakeRAM(buffer.data()));
    Invalidate();
}

uint8_t TextErrorUrlWindow::wrap(size_t max_cols) {
    const std::string_view link { buffer.data() };
    if (link.find('\n') != std::string_view::npos) {
        return 2;
    }
    if (link.size() <= max_cols || max_cols == 0) {
        return 1;
    }

    const size_t slash = link.rfind('/', max_cols - 1);
    if (slash == std::string_view::npos) {
        return 1;
    }

    // Shift the rest of the link, including the terminating null, to make room for the line break
    const auto line_end = buffer.begin() + slash + 1;
    std::copy_backward(line_end, buffer.begin() + link.size() + 1, buffer.begin() + link.size() + 2);
    *line_end = '\n';

    set_is_multiline(true);
    SetText(string_view_utf8::MakeRAM(buffer.data()));
    Invalidate();
    return 2;
}
