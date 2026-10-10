#include "filament_swatch.hpp"

#include <display.hpp>
#include <display_helper.h>
#include <fonts.hpp>
#include <guiconfig/GuiDefaults.hpp>

namespace {

constexpr int16_t size = 14;
constexpr int16_t spacing = 6;

} // namespace

namespace filament_swatch {

void draw(Rect16 rect, Color color_back, Color color) {
    const Rect16 outer = Rect16::fromLTWH(rect.Left() + (rect.Width() - size) / 2, rect.Top() + (rect.Height() - size) / 2, size, size);
    display::draw_rounded_rect(outer, color_back, COLOR_GRAY, GuiDefaults::MenuItemCornerRadius, MIC_ALL_CORNERS);

    const Rect16 inner = Rect16::fromLTWH(outer.Left() + 1, outer.Top() + 1, size - 2, size - 2);
    display::draw_rounded_rect(inner, COLOR_GRAY, color, GuiDefaults::MenuItemCornerRadius, MIC_ALL_CORNERS);
}

uint16_t width_with_text(const std::optional<Color> &swatch, const string_view_utf8 &text) {
    return (swatch ? size + spacing : 0) + resource_font(GuiDefaults::FontMenuItems)->w * text.computeNumUtf8Chars();
}

void print_with_text(Rect16 rect, const std::optional<Color> &swatch, const string_view_utf8 &text, Color color_text, Color color_back) {
    if (swatch) {
        draw(Rect16::fromLTWH(rect.Left(), rect.Top(), size, rect.Height()), color_back, *swatch);
        rect = Rect16::fromLTRB(rect.Left() + size + spacing, rect.Top(), rect.EndPoint().x, rect.EndPoint().y);
    }
    render_text_align(rect, text, GuiDefaults::FontMenuItems, color_back, color_text, {}, Align_t::RightCenter(), false);
}

} // namespace filament_swatch
