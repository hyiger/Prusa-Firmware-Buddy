/// @file
#pragma once

#include <array>
#include <optional>
#include <utility>

#include <filament_library.hpp>
#include "filament_swatch.hpp"
#include <filament_list.hpp>
#include <gui/menu_item/menu_item_select_menu.hpp>
#include <i_window_menu_item.hpp>
#include <inplace_vector.hpp>
#include <meta_utils.hpp>
#include <screen_menu.hpp>
#include <tool_index.hpp>
#include <window_menu_virtual.hpp>
#include <WindowMenuItems.hpp>

/// Filament menu entry that opens ScreenFilamentSlots
class MI_FILAMENT_SLOTS final : public IWindowMenuItem {
public:
    MI_FILAMENT_SLOTS();

protected:
    void click(IWindowMenu &) override;
};

namespace screen_filament_slots {

/// "Tool N" with the vendor, type and color of its filament. Opens ScreenFilamentSlot.
/// Disabled when the tool has no filament, as vendor and color describe a loaded spool.
class MI_SLOT : public IWindowMenuItem {
public:
    MI_SLOT(uint8_t tool);

protected:
    void click(IWindowMenu &) override;
    void printExtension(Rect16 extension_rect, Color color_text, Color color_back, ropfn raster_op) const override;

private:
    filament_swatch::Arrow arrow() const;

    VirtualToolIndex tool_;
    VirtualToolIndex::DisplayNameParams label_params_;
    std::array<char, 32> value_;
    std::optional<Color> color_;
};

template <typename>
struct ScreenFilamentSlots_ {};

template <size_t... i>
struct ScreenFilamentSlots_<std::index_sequence<i...>> {
    using T = ScreenMenu<GuiDefaults::MenuFooter, MI_RETURN, WithConstructorArgs<MI_SLOT, i>...>;
};

} // namespace screen_filament_slots

/// Lists the tools with the vendor, type and color of their filaments
class ScreenFilamentSlots final : public screen_filament_slots::ScreenFilamentSlots_<std::make_index_sequence<VirtualToolIndex::count>>::T {
public:
    ScreenFilamentSlots();
};

namespace screen_filament_slot {

class MI_VENDOR final : public MenuItemSelectMenu {
public:
    MI_VENDOR();

    void set_tool(VirtualToolIndex tool);

    int item_count() const final;
    string_view_utf8 build_item_text(int index, ItemTextParams &params) const final;

protected:
    bool on_item_selected(const OnItemSelectedArgs &args) final;

private:
    VirtualToolIndex tool_ = VirtualToolIndex::from_raw(0);

    /// None, then the visible vendors, plus the assigned one if it is hidden
    stdext::inplace_vector<FilamentVendor, FilamentVendor::total_count + 1> items_;
};

/// Changes the loaded filament type without loading anything, like M865 L
class MI_TYPE final : public MenuItemSelectMenu {
public:
    MI_TYPE();

    void set_tool(VirtualToolIndex tool);

    int item_count() const final;
    string_view_utf8 build_item_text(int index, ItemTextParams &params) const final;

protected:
    bool on_item_selected(const OnItemSelectedArgs &args) final;

private:
    VirtualToolIndex tool_ = VirtualToolIndex::from_raw(0);
    FilamentList items_;
};

/// Shows the assigned color, opens ScreenFilamentSlotColor
class MI_COLOR final : public IWindowMenuItem {
public:
    MI_COLOR();

    void set_tool(VirtualToolIndex tool);

protected:
    void click(IWindowMenu &) override;
    void printExtension(Rect16 extension_rect, Color color_text, Color color_back, ropfn raster_op) const override;

private:
    VirtualToolIndex tool_ = VirtualToolIndex::from_raw(0);
    FilamentLibraryName name_;
    std::optional<Color> color_;
};

using ScreenFilamentSlot_ = ScreenMenu<GuiDefaults::MenuFooter, MI_RETURN, MI_VENDOR, MI_TYPE, MI_COLOR>;

} // namespace screen_filament_slot

/// Vendor, type and color of the filament loaded in one tool
class ScreenFilamentSlot final : public screen_filament_slot::ScreenFilamentSlot_ {
public:
    ScreenFilamentSlot(VirtualToolIndex tool);

private:
    VirtualToolIndex::DisplayNameParams title_params_;
    std::array<char, 32> title_ {};
};

namespace screen_filament_slot_color {

/// Color with its swatch. Assigns the color to the menu's tool and closes the screen.
class MI_COLOR_OPTION final : public IWindowMenuItem {
public:
    MI_COLOR_OPTION(FilamentColor color);

protected:
    void click(IWindowMenu &) override;
    void printIcon(Rect16 icon_rect, ropfn raster_op, Color color_back) const override;

private:
    FilamentColor color_;
    FilamentLibraryName name_;
    std::optional<Color> swatch_;
};

class WindowMenuColors final : public WindowMenuVirtual {
public:
    WindowMenuColors(window_t *parent, Rect16 rect);

    void set_tool(VirtualToolIndex tool);

    VirtualToolIndex tool() const {
        return tool_;
    }

    int item_count() const final;

protected:
    void setup_item(ItemVariant &variant, int index) final;

private:
    VirtualToolIndex tool_ = VirtualToolIndex::from_raw(0);

    /// None, then the visible colors, plus the assigned one if it is hidden
    stdext::inplace_vector<FilamentColor, FilamentColor::total_count + 1> items_;
};

} // namespace screen_filament_slot_color

/// Color palette for one tool
class ScreenFilamentSlotColor final : public ScreenMenuBase<screen_filament_slot_color::WindowMenuColors> {
public:
    ScreenFilamentSlotColor(VirtualToolIndex tool);
};
