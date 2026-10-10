/// @file
#pragma once

#include <optional>

#include <color.hpp>
#include <Rect16.h>
#include <string_view_utf8.hpp>

namespace filament_swatch {

/// Draws a color swatch centered in \p rect
void draw(Rect16 rect, Color color_back, Color color);

/// \returns width needed by print_with_text
uint16_t width_with_text(const std::optional<Color> &swatch, const string_view_utf8 &text);

/// Draws an optional swatch at the left of \p rect and \p text right-aligned in the rest, for menu item extensions
void print_with_text(Rect16 rect, const std::optional<Color> &swatch, const string_view_utf8 &text, Color color_text, Color color_back);

} // namespace filament_swatch
