#include "screen_filament_slots.hpp"

#include <cstdio>

#include <algorithm_extensions.hpp>
#include <config_store/store_instance.hpp>
#include <display.hpp>
#include <display_helper.h>
#include <filament.hpp>
#include <fonts.hpp>
#include <gui/ScreenHandler.hpp>
#include <marlin_client.hpp>
#include <multi_filament_change.hpp>
#include <utils/string_builder.hpp>

namespace {

constexpr int16_t swatch_size = 14;
constexpr int16_t swatch_spacing = 6;

void draw_swatch(Rect16 rect, Color color_back, Color color) {
    const Rect16 outer = Rect16::fromLTWH(rect.Left() + (rect.Width() - swatch_size) / 2, rect.Top() + (rect.Height() - swatch_size) / 2, swatch_size, swatch_size);
    display::draw_rounded_rect(outer, color_back, COLOR_GRAY, GuiDefaults::MenuItemCornerRadius, MIC_ALL_CORNERS);

    const Rect16 inner = Rect16::fromLTWH(outer.Left() + 1, outer.Top() + 1, swatch_size - 2, swatch_size - 2);
    display::draw_rounded_rect(inner, COLOR_GRAY, color, GuiDefaults::MenuItemCornerRadius, MIC_ALL_CORNERS);
}

uint16_t swatch_and_text_width(const std::optional<Color> &swatch, const string_view_utf8 &text) {
    return (swatch ? swatch_size + swatch_spacing : 0) + resource_font(GuiDefaults::FontMenuItems)->w * text.computeNumUtf8Chars();
}

void print_swatch_and_text(Rect16 rect, const std::optional<Color> &swatch, const string_view_utf8 &text, Color color_text, Color color_back) {
    if (swatch) {
        draw_swatch(Rect16::fromLTWH(rect.Left(), rect.Top(), swatch_size, rect.Height()), color_back, *swatch);
        rect = Rect16::fromLTRB(rect.Left() + swatch_size + swatch_spacing, rect.Top(), rect.EndPoint().x, rect.EndPoint().y);
    }
    render_text_align(rect, text, GuiDefaults::FontMenuItems, color_back, color_text, {}, Align_t::RightCenter(), false);
}

string_view_utf8 copy_name(const FilamentLibraryName &name, MenuItemSelectMenu::ItemTextParams &params) {
    snprintf(params.buffer.data(), params.buffer.size(), "%s", name.data());
    return string_view_utf8::MakeRAM(params.buffer.data());
}

} // namespace

// MI_FILAMENT_SLOTS
// =============================================================
MI_FILAMENT_SLOTS::MI_FILAMENT_SLOTS()
    : IWindowMenuItem(_("Filament slots"), nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::yes) {
}

void MI_FILAMENT_SLOTS::click(IWindowMenu &) {
    Screens::Access()->Open<ScreenFilamentSlots>();
}

// ScreenFilamentSlots
// =============================================================
namespace screen_filament_slots {

MI_SLOT::MI_SLOT(uint8_t tool)
    : IWindowMenuItem({}, nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::no)
    , tool_(VirtualToolIndex::from_raw(tool)) {
    SetLabel(tool_.display_name(label_params_));
    set_is_hidden(!tool_.is_enabled());

    const FilamentType type = FilamentType::for_tool(tool_);
    StringBuilder sb(value_);
    if (type == FilamentType::none) {
        sb.append_string_view(_("Empty"));
        set_enabled(false);

    } else {
        const auto spool = FilamentSpool::for_tool(tool_);
        if (spool.vendor) {
            sb.append_string(spool.vendor.name().data());
            sb.append_char(' ');
        }
        sb.append_string(type.parameters().name.data());
        color_ = spool.color.color();
    }

    extension_width = swatch_and_text_width(color_, string_view_utf8::MakeRAM(value_.data()));
}

void MI_SLOT::click(IWindowMenu &) {
    Screens::Access()->Open(ScreenFactory::ScreenWithArg<ScreenFilamentSlot>(tool_));
}

void MI_SLOT::printExtension(Rect16 extension_rect, Color color_text, Color color_back, [[maybe_unused]] ropfn raster_op) const {
    print_swatch_and_text(extension_rect, color_, string_view_utf8::MakeRAM(value_.data()), color_text, color_back);
}

} // namespace screen_filament_slots

ScreenFilamentSlots::ScreenFilamentSlots()
    : ScreenMenu(_("FILAMENT SLOTS")) {
}

// ScreenFilamentSlot
// =============================================================
namespace screen_filament_slot {

MI_VENDOR::MI_VENDOR()
    : MenuItemSelectMenu(_("Vendor")) {
}

void MI_VENDOR::set_tool(VirtualToolIndex tool) {
    tool_ = tool;

    const FilamentVendor assigned = FilamentSpool::for_tool(tool).vendor;
    items_.clear();
    items_.push_back({});
    for (const auto vendor : FilamentVendor::all()) {
        if (vendor.is_visible() || vendor == assigned) {
            items_.push_back(vendor);
        }
    }

    set_current_item(stdext::index_of(items_, assigned));
}

int MI_VENDOR::item_count() const {
    return items_.size();
}

string_view_utf8 MI_VENDOR::build_item_text(int index, ItemTextParams &params) const {
    const FilamentVendor vendor = items_[index];
    return vendor ? copy_name(vendor.name(), params) : _("None");
}

bool MI_VENDOR::on_item_selected(const OnItemSelectedArgs &args) {
    auto spool = FilamentSpool::for_tool(tool_);
    spool.vendor = items_[args.new_index];
    FilamentSpool::set_for_tool(tool_, spool);
    return true;
}

MI_TYPE::MI_TYPE()
    : MenuItemSelectMenu(_("Filament")) {
}

void MI_TYPE::set_tool(VirtualToolIndex tool) {
    tool_ = tool;

    // The loaded filament is always at position 0, even if hidden or incompatible
    generate_filament_list(items_, { .enforce_first_item = FilamentType::for_tool(tool), .compatible_with_tool = tool });
    set_current_item(0);

    // Changing the filament type mid-print would change the temperatures the print runs at
    set_enabled(!marlin_client::is_printing());
}

int MI_TYPE::item_count() const {
    return items_.size();
}

string_view_utf8 MI_TYPE::build_item_text(int index, ItemTextParams &params) const {
    snprintf(params.buffer.data(), params.buffer.size(), "%s", items_[index].parameters().name.data());
    return string_view_utf8::MakeRAM(params.buffer.data());
}

bool MI_TYPE::on_item_selected(const OnItemSelectedArgs &args) {
    const FilamentType new_type = items_[args.new_index];

    const multi_filament_change::ConfigItem change {
        .action = multi_filament_change::Action::change,
        .new_filament = new_type,
        .color = {},
    };
    if (!multi_filament_change::gui_config_confirm_incompatibilities(change, tool_, Response::Cancel, buddy::compatibility_checks::CompatibilityLevel::compatible_with_reminder)) {
        return false;
    }

    config_store().set_filament_type(tool_, new_type);
    return true;
}

MI_COLOR::MI_COLOR()
    : IWindowMenuItem(_("Color"), nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::no) {
}

void MI_COLOR::set_tool(VirtualToolIndex tool) {
    tool_ = tool;

    const FilamentColor color = FilamentSpool::for_tool(tool).color;
    name_ = color.name();
    color_ = color.color();

    extension_width = swatch_and_text_width(color_, color_ ? string_view_utf8::MakeRAM(name_.data()) : _("None"));
    Invalidate();
}

void MI_COLOR::click(IWindowMenu &) {
    Screens::Access()->Open(ScreenFactory::ScreenWithArg<ScreenFilamentSlotColor>(tool_));
}

void MI_COLOR::printExtension(Rect16 extension_rect, Color color_text, Color color_back, [[maybe_unused]] ropfn raster_op) const {
    print_swatch_and_text(extension_rect, color_, color_ ? string_view_utf8::MakeRAM(name_.data()) : _("None"), color_text, color_back);
}

} // namespace screen_filament_slot

ScreenFilamentSlot::ScreenFilamentSlot(VirtualToolIndex tool)
    : screen_filament_slot::ScreenFilamentSlot_(string_view_utf8::MakeNULLSTR()) {
    header.SetText(tool.display_name(title_params_));

    using namespace screen_filament_slot;
    Item<MI_VENDOR>().set_tool(tool);
    Item<MI_TYPE>().set_tool(tool);
    Item<MI_COLOR>().set_tool(tool);
}

// ScreenFilamentSlotColor
// =============================================================
namespace screen_filament_slot_color {

MI_COLOR_OPTION::MI_COLOR_OPTION(FilamentColor color)
    : IWindowMenuItem({}, nullptr)
    , color_(color)
    , name_(color.name())
    , swatch_(color.color()) {
    SetLabel(color ? string_view_utf8::MakeRAM(name_.data()) : _("None"));
}

void MI_COLOR_OPTION::click(IWindowMenu &menu) {
    const VirtualToolIndex tool = static_cast<WindowMenuColors &>(menu).tool();
    auto spool = FilamentSpool::for_tool(tool);
    spool.color = color_;
    FilamentSpool::set_for_tool(tool, spool);
    Screens::Access()->Close();
}

void MI_COLOR_OPTION::printIcon(Rect16 icon_rect, [[maybe_unused]] ropfn raster_op, Color color_back) const {
    if (swatch_) {
        draw_swatch(icon_rect, color_back, *swatch_);
    }
}

WindowMenuColors::WindowMenuColors(window_t *parent, Rect16 rect)
    : WindowMenuVirtual(parent, rect, CloseScreenReturnBehavior::yes) {
}

void WindowMenuColors::set_tool(VirtualToolIndex tool) {
    tool_ = tool;

    const FilamentColor assigned = FilamentSpool::for_tool(tool).color;
    items_.clear();
    items_.push_back({});
    for (const auto color : FilamentColor::all()) {
        if (color.is_visible() || color == assigned) {
            items_.push_back(color);
        }
    }

    setup_items();

    // +1 for the return item
    move_focus_to_index(stdext::index_of(items_, assigned) + 1);
}

int WindowMenuColors::item_count() const {
    return items_.size() + 1;
}

void WindowMenuColors::setup_item(ItemVariant &variant, int index) {
    if (index == 0) {
        variant.emplace<MI_RETURN>();
    } else {
        variant.emplace<MI_COLOR_OPTION>(items_[index - 1]);
    }
}

} // namespace screen_filament_slot_color

ScreenFilamentSlotColor::ScreenFilamentSlotColor(VirtualToolIndex tool)
    : ScreenMenuBase(nullptr, _("COLOR"), EFooter::Off) {
    menu.menu.set_tool(tool);
}
