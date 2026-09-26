#pragma once

#include "error_codes.hpp"
#include "window_text.hpp"
#include <array>

class TextErrorUrlWindow : public window_text_t {
private:
    std::array<char, 56> buffer;

public:
    TextErrorUrlWindow(window_t *parent, Rect16 rect, ErrCode ec);
    TextErrorUrlWindow(window_t *parent, Rect16 rect);

    void set_error_code(ErrCode ec);

    /// Breaks the link after its last '/' that keeps the first line within \p max_cols,
    /// so a long help link fits a narrow window. Call after set_error_code().
    /// @return number of lines the link takes
    uint8_t wrap(size_t max_cols);
};
