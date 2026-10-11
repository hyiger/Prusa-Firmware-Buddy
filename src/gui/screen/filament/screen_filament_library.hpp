/// @file
/// Screens for managing the filament vendor and color libraries, in Filament management
#pragma once

#include <array>
#include <optional>

#include <filament_library.hpp>
#include <gui/ScreenHandler.hpp>
#include <i_window_menu_item.hpp>
#include <screen_menu.hpp>
#include <window_menu_virtual.hpp>
#include <WindowMenuItems.hpp>

namespace screen_filament_library {

/// Toggles whether a vendor or color is listed in the selection lists
template <typename Item>
class MI_VISIBILITY final : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_VISIBILITY(Item item);

protected:
    void OnChange(size_t) override;
    void printIcon(Rect16 icon_rect, ropfn raster_op, Color color_back) const override;

private:
    Item item_;
    FilamentLibraryName name_;
};

template <typename Item>
class WindowMenuVisibility final : public WindowMenuVirtual {
public:
    WindowMenuVisibility(window_t *parent, Rect16 rect);

    int item_count() const final;

protected:
    void setup_item(ItemVariant &variant, int index) final;
};

/// Renames a user vendor slot
class MI_EDIT_VENDOR final : public IWindowMenuItem {
public:
    MI_EDIT_VENDOR(FilamentVendor vendor);

protected:
    void click(IWindowMenu &) override;

private:
    FilamentVendor vendor_;
    FilamentLibraryName name_;
};

class WindowMenuEditVendors final : public WindowMenuVirtual {
public:
    WindowMenuEditVendors(window_t *parent, Rect16 rect);

    int item_count() const final;

protected:
    void setup_item(ItemVariant &variant, int index) final;
};

/// User color slot with its swatch, opens ScreenFilamentColorDetail
class MI_EDIT_COLOR final : public IWindowMenuItem {
public:
    MI_EDIT_COLOR(FilamentColor color);

protected:
    void click(IWindowMenu &) override;
    void printIcon(Rect16 icon_rect, ropfn raster_op, Color color_back) const override;

private:
    FilamentColor color_;
    FilamentLibraryName name_;
    std::optional<Color> swatch_;
};

class WindowMenuEditColors final : public WindowMenuVirtual {
public:
    WindowMenuEditColors(window_t *parent, Rect16 rect);

    int item_count() const final;

protected:
    void setup_item(ItemVariant &variant, int index) final;
};

class MI_COLOR_NAME final : public IWindowMenuItem {
public:
    MI_COLOR_NAME();

    void set_color(FilamentColor color);

protected:
    void click(IWindowMenu &) override;
    void printExtension(Rect16 extension_rect, Color color_text, Color color_back, ropfn raster_op) const override;

private:
    FilamentColor color_;
    FilamentLibraryName name_;
};

class MI_COLOR_HEX final : public IWindowMenuItem {
public:
    MI_COLOR_HEX();

    void set_color(FilamentColor color);

protected:
    void click(IWindowMenu &) override;
    void printExtension(Rect16 extension_rect, Color color_text, Color color_back, ropfn raster_op) const override;

private:
    FilamentColor color_;
    std::optional<Color> swatch_;
    ColorHexString hex_;
};

/// Menu item labeled \p label_ that opens \p Screen
template <auto label_, class Screen>
class MI_OPEN final : public IWindowMenuItem {
public:
    MI_OPEN()
        : IWindowMenuItem(_(label_), nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::yes) {}

protected:
    void click(IWindowMenu &) override {
        Screens::Access()->Open<Screen>();
    }
};

} // namespace screen_filament_library

class ScreenFilamentVendorsVisibility final : public ScreenMenuBase<screen_filament_library::WindowMenuVisibility<FilamentVendor>> {
public:
    ScreenFilamentVendorsVisibility();
};

class ScreenFilamentColorsVisibility final : public ScreenMenuBase<screen_filament_library::WindowMenuVisibility<FilamentColor>> {
public:
    ScreenFilamentColorsVisibility();
};

/// Lists the user vendor slots for renaming
class ScreenFilamentVendorsEdit final : public ScreenMenuBase<screen_filament_library::WindowMenuEditVendors> {
public:
    ScreenFilamentVendorsEdit();
};

/// Lists the user color slots for editing
class ScreenFilamentColorsEdit final : public ScreenMenuBase<screen_filament_library::WindowMenuEditColors> {
public:
    ScreenFilamentColorsEdit();
};

/// Name and hex code of a user color slot
class ScreenFilamentColorDetail final : public ScreenMenu<EFooter::Off, MI_RETURN, screen_filament_library::MI_COLOR_NAME, screen_filament_library::MI_COLOR_HEX> {
public:
    ScreenFilamentColorDetail(FilamentColor color);
};

using MI_EDIT_FILAMENT_VENDORS = screen_filament_library::MI_OPEN<N_("Edit Vendors"), ScreenFilamentVendorsEdit>;
using MI_FILAMENT_VENDORS_VISIBILITY = screen_filament_library::MI_OPEN<N_("Enable Vendors"), ScreenFilamentVendorsVisibility>;
using MI_EDIT_FILAMENT_COLORS = screen_filament_library::MI_OPEN<N_("Edit Colors"), ScreenFilamentColorsEdit>;
using MI_FILAMENT_COLORS_VISIBILITY = screen_filament_library::MI_OPEN<N_("Enable Colors"), ScreenFilamentColorsVisibility>;
