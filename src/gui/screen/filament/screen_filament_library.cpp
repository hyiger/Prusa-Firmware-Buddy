#include "screen_filament_library.hpp"
#include "filament_swatch.hpp"

#include <dialog_text_input.hpp>
#include <window_msgbox.hpp>

namespace screen_filament_library {

namespace {

    /// Lets the user rename \p item with the keyboard until the name is valid or the user cancels.
    /// A renamed item is made visible, since the user is setting it up to be used.
    template <typename Item>
    void gui_rename(Item item, const string_view_utf8 &prompt) {
        FilamentLibraryName name = item.name();
        while (true) {
            if (!DialogTextInput::exec(prompt, name.data_)) {
                return;
            }

            if (const auto r = item.can_be_renamed_to(name); !r) {
                MsgBoxWarning(_(r.error()), Responses_Ok);
                continue;
            }

            item.set_name(name);
            item.set_visible(true);
            return;
        }
    }

} // namespace

// Visibility
// =============================================================
template <typename Item>
MI_VISIBILITY<Item>::MI_VISIBILITY(Item item)
    : WI_ICON_SWITCH_OFF_ON_t(item.is_visible(), {})
    , item_(item)
    , name_(item.name()) {
    SetLabel(string_view_utf8::MakeRAM(name_.data()));
}

template <typename Item>
void MI_VISIBILITY<Item>::OnChange(size_t) {
    item_.set_visible(value());
}

template <typename Item>
void MI_VISIBILITY<Item>::printIcon(Rect16 icon_rect, [[maybe_unused]] ropfn raster_op, Color color_back) const {
    if constexpr (std::is_same_v<Item, FilamentColor>) {
        filament_swatch::draw(icon_rect, color_back, *item_.color());
    }
}

template <typename Item>
WindowMenuVisibility<Item>::WindowMenuVisibility(window_t *parent, Rect16 rect)
    : WindowMenuVirtual(parent, rect, CloseScreenReturnBehavior::yes) {
    setup_items();
}

template <typename Item>
int WindowMenuVisibility<Item>::item_count() const {
    return Item::total_count + 1;
}

template <typename Item>
void WindowMenuVisibility<Item>::setup_item(ItemVariant &variant, int index) {
    if (index == 0) {
        variant.emplace<MI_RETURN>();
    } else {
        variant.emplace<MI_VISIBILITY<Item>>(Item::all()[index - 1]);
    }
}

template class WindowMenuVisibility<FilamentVendor>;
template class WindowMenuVisibility<FilamentColor>;

// Vendors
// =============================================================
MI_EDIT_VENDOR::MI_EDIT_VENDOR(FilamentVendor vendor)
    : IWindowMenuItem({}, nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::yes)
    , vendor_(vendor)
    , name_(vendor.name()) {
    SetLabel(string_view_utf8::MakeRAM(name_.data()));
}

void MI_EDIT_VENDOR::click(IWindowMenu &) {
    gui_rename(vendor_, _("Vendor name"));
    name_ = vendor_.name();
    SetLabel(string_view_utf8::MakeRAM(name_.data()));
    Invalidate();
}

WindowMenuEditVendors::WindowMenuEditVendors(window_t *parent, Rect16 rect)
    : WindowMenuVirtual(parent, rect, CloseScreenReturnBehavior::yes) {
    setup_items();
}

int WindowMenuEditVendors::item_count() const {
    return user_filament_vendor_count + 1;
}

void WindowMenuEditVendors::setup_item(ItemVariant &variant, int index) {
    if (index == 0) {
        variant.emplace<MI_RETURN>();
    } else {
        variant.emplace<MI_EDIT_VENDOR>(FilamentVendor::user(index - 1));
    }
}

// Colors
// =============================================================
MI_EDIT_COLOR::MI_EDIT_COLOR(FilamentColor color)
    : IWindowMenuItem({}, nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::yes)
    , color_(color)
    , name_(color.name())
    , swatch_(color.color()) {
    SetLabel(string_view_utf8::MakeRAM(name_.data()));
}

void MI_EDIT_COLOR::click(IWindowMenu &) {
    Screens::Access()->Open(ScreenFactory::ScreenWithArg<ScreenFilamentColorDetail>(color_));
}

void MI_EDIT_COLOR::printIcon(Rect16 icon_rect, [[maybe_unused]] ropfn raster_op, Color color_back) const {
    if (swatch_) {
        filament_swatch::draw(icon_rect, color_back, *swatch_);
    }
}

WindowMenuEditColors::WindowMenuEditColors(window_t *parent, Rect16 rect)
    : WindowMenuVirtual(parent, rect, CloseScreenReturnBehavior::yes) {
    setup_items();
}

int WindowMenuEditColors::item_count() const {
    return user_filament_color_count + 1;
}

void WindowMenuEditColors::setup_item(ItemVariant &variant, int index) {
    if (index == 0) {
        variant.emplace<MI_RETURN>();
    } else {
        variant.emplace<MI_EDIT_COLOR>(FilamentColor::user(index - 1));
    }
}

MI_COLOR_NAME::MI_COLOR_NAME()
    : IWindowMenuItem(_("Name"), nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::no) {
}

void MI_COLOR_NAME::set_color(FilamentColor color) {
    color_ = color;
    name_ = color.name();
    extension_width = filament_swatch::width_with_text(std::nullopt, string_view_utf8::MakeRAM(name_.data()));
    Invalidate();
}

void MI_COLOR_NAME::click(IWindowMenu &) {
    gui_rename(color_, _("Color name"));
    set_color(color_);
}

void MI_COLOR_NAME::printExtension(Rect16 extension_rect, Color color_text, Color color_back, [[maybe_unused]] ropfn raster_op) const {
    filament_swatch::print_with_text(extension_rect, std::nullopt, string_view_utf8::MakeRAM(name_.data()), color_text, color_back);
}

MI_COLOR_HEX::MI_COLOR_HEX()
    : IWindowMenuItem(_("Hex Code"), nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::no) {
}

void MI_COLOR_HEX::set_color(FilamentColor color) {
    color_ = color;
    swatch_ = color.color();
    hex_ = color_to_hex(swatch_.value_or(COLOR_BLACK));
    extension_width = filament_swatch::width_with_text(swatch_, string_view_utf8::MakeRAM(hex_.data()));
    Invalidate();
}

void MI_COLOR_HEX::click(IWindowMenu &) {
    ColorHexString input = hex_;
    while (true) {
        if (!DialogTextInput::exec(_("Hex code"), input)) {
            return;
        }

        if (const auto color = color_from_hex(input.data())) {
            color_.set_color(*color);
            color_.set_visible(true);
            set_color(color_);
            return;
        }

        MsgBoxWarning(_("Enter the color as #RRGGBB"), Responses_Ok);
    }
}

void MI_COLOR_HEX::printExtension(Rect16 extension_rect, Color color_text, Color color_back, [[maybe_unused]] ropfn raster_op) const {
    filament_swatch::print_with_text(extension_rect, swatch_, string_view_utf8::MakeRAM(hex_.data()), color_text, color_back);
}

} // namespace screen_filament_library

// Screens
// =============================================================
ScreenFilamentVendorsVisibility::ScreenFilamentVendorsVisibility()
    : ScreenMenuBase(nullptr, _("ENABLE VENDORS"), EFooter::Off) {
}

ScreenFilamentColorsVisibility::ScreenFilamentColorsVisibility()
    : ScreenMenuBase(nullptr, _("ENABLE COLORS"), EFooter::Off) {
}

ScreenFilamentVendorsEdit::ScreenFilamentVendorsEdit()
    : ScreenMenuBase(nullptr, _("EDIT VENDORS"), EFooter::Off) {
}

ScreenFilamentColorsEdit::ScreenFilamentColorsEdit()
    : ScreenMenuBase(nullptr, _("EDIT COLORS"), EFooter::Off) {
}

ScreenFilamentColorDetail::ScreenFilamentColorDetail(FilamentColor color)
    : ScreenMenu(_("COLOR DETAIL")) {
    Item<screen_filament_library::MI_COLOR_NAME>().set_color(color);
    Item<screen_filament_library::MI_COLOR_HEX>().set_color(color);
}
