
#include "screen_menu_filament_changeall.hpp"

#include <algorithm>
#include <algorithm_extensions.hpp>

#include <ScreenHandler.hpp>
#include <img_resources.hpp>
#include <marlin_client.hpp>
#include <option/has_mmu2.h>
#include <config_store/store_instance.hpp>
#include <utils/string_builder.hpp>
#include <algorithm_extensions.hpp>
#include <filament_list.hpp>
#include <option/has_toolchanger.h>
#include <bsod/bsod.h>

#if HAS_FILAMENT_SLOTS()
    #include <gui/screen/filament/filament_library_dialog.hpp>
    #include <gui/screen/filament/filament_swatch.hpp>
#endif

using namespace multi_filament_change;

MI_ActionSelect::MI_ActionSelect(uint8_t tool_ix)
    : MenuItemSelectMenu({})
    , tool_filter_ { VirtualToolIndex::from_raw(tool_ix) } {
    const auto tool = VirtualToolIndex::from_raw(tool_ix);
    has_filament_loaded = (config_store().get_filament_type(tool) != FilamentType::none);
    set_is_hidden(!tool.is_enabled());
    SetLabel(tool.display_name(label_params));
}

MI_ActionSelect::MI_ActionSelect(SetAllToMode)
    : MenuItemSelectMenu(_("Set All To"))
    , tool_filter_ { AllTools {} } {
    set_behavior(Behavior::select_only);

    // Necessary to generate filament list
    set_config({});
}

void MI_ActionSelect::set_config(const ConfigItem &set) {
    const GenerateFilamentListConfig gen_cfg {
        // target filament is always present at position 0,
        // even if hidden or incompatible
        // removing the previously-selected value would silently change the menu's current selection.
        .enforce_first_item = set.new_filament,
        // compatible_with_tool: hide filaments the tool's hotend cannot reach (e.g. PPS on standard hotend).
        .compatible_with_tool = stdext::to_variant(tool_filter_),
    };
    generate_filament_list(filament_list, gen_cfg);

    index_mapping.set_section_size<Action::change>(filament_list.size());

    color = set.color;
#if HAS_FILAMENT_SLOTS()
    spool_ = set.spool;
    InValidateIcon();
#endif
    set_current_item([&] -> size_t {
        switch (set.action) {
        case Action::keep:
            return index_mapping.to_index<Action::keep>();

        case Action::unload:
            return index_mapping.to_index<Action::unload>();

        case Action::change:
            return index_mapping.to_index<Action::change>(stdext::index_of(filament_list, set.new_filament));
        }

        std::abort();
    }());
}

ConfigItem MI_ActionSelect::config(int item_index) const {
    const auto mapping = index_mapping.from_index(item_index);
    ConfigItem result {
        .action = mapping.item,
        .new_filament = (mapping.item == Action::change) ? filament_list[mapping.pos_in_section] : FilamentType::none,
        .color = color
    };
#if HAS_FILAMENT_SLOTS()
    if (mapping.item == Action::change) {
        result.spool = spool_;
    }
#endif
    return result;
}

int MI_ActionSelect::item_count() const {
    return index_mapping.total_item_count();
}

string_view_utf8 MI_ActionSelect::build_item_text(int index, MenuItemSelectMenu::ItemTextParams &params) const {
    const auto mapping = index_mapping.from_index(index);
    switch (mapping.item) {

    case Action::keep:
        return _("Don't change");

    case Action::unload:
        return _("Unload");

    case Action::change: {
        const auto fmt = has_filament_loaded ? N_("Change to %s") : N_("Load %s");
        return _(fmt).formatted(params, filament_list[mapping.pos_in_section].parameters().name.data());
    }
    }

    bsod_unreachable();
}

bool MI_ActionSelect::on_item_selected(const OnItemSelectedArgs &args) {
    const auto new_config_item = this->config(args.new_index);

    if (std::holds_alternative<AllTools>(tool_filter_)) {
        auto &menu = static_cast<MenuMultiFilamentChange &>(args.menu);
        auto new_config = menu.configuration();

        // Skip reminders here, show them only on the final screen confirmation
        if (!gui_config_confirm_incompatibilities(new_config_item, AllTools {}, Response::Cancel, buddy::compatibility_checks::CompatibilityLevel::compatible_with_reminder)) {
            return false;
        }

        for (auto tool : VirtualToolIndex::all().skip_all_disabled()) {
            auto &config_item = new_config[tool];

            // Keep the color
            const auto orig_color = config_item.color;
            config_item = new_config_item;
            config_item.color = orig_color;
        }

#if HAS_FILAMENT_SLOTS()
        if (new_config_item.action == Action::change && !pick_spools_for_all(new_config, args.menu)) {
            reopen_at_ = args.new_index;
            return false;
        }
#endif

        menu.set_configuration(new_config);

    } else {
        // Skip reminders here, show them only on the final screen confirmation
        if (!gui_config_confirm_incompatibilities(new_config_item, std::get<VirtualToolIndex>(tool_filter_), Response::Cancel, buddy::compatibility_checks::CompatibilityLevel::compatible_with_reminder)) {
            return false;
        }

#if HAS_FILAMENT_SLOTS()
        if (new_config_item.action == Action::change && !pick_spool(args.menu)) {
            reopen_at_ = args.new_index;
            return false;
        }
#endif

        // Just let current_item to be updated by the parent, will be picked up by the owning menu
    }

    return true;
}

#if HAS_FILAMENT_SLOTS()
void MI_ActionSelect::click(IWindowMenu &menu) {
    // Going back from the vendor returns to the filament list, as when loading into a single tool
    do {
        MenuItemSelectMenu::click(menu);
    } while (reopen_at_);
}

void MI_ActionSelect::on_item_reselected(IWindowMenu &menu) {
    // Picking the same filament again changes only its vendor and color
    if (std::holds_alternative<VirtualToolIndex>(tool_filter_) && config().action == Action::change && !pick_spool(menu)) {
        reopen_at_ = current_item();
    }
}

int MI_ActionSelect::submenu_focus_item() {
    return std::exchange(reopen_at_, std::nullopt).value_or(current_item());
}

bool MI_ActionSelect::pick_spool(IWindowMenu &menu) {
    const auto tool = std::get<VirtualToolIndex>(tool_filter_);
    const bool has_pick = config().action == Action::change && spool_ != ConfigItem::unpicked_spool;
    const auto prev_focus = menu.focused_item_index();
    const auto picked = filament_library_dialog::pick_spool_for_tool(tool, has_pick ? spool_ : FilamentSpool::previous_for_tool(tool));

    // Opening a dialog with a menu moves the focus of the current menu
    menu.move_focus_to_index(prev_focus);

    if (!picked) {
        return false;
    }
    spool_ = *picked;
    InValidateIcon();
    return true;
}

bool MI_ActionSelect::pick_spools_for_all(Config &config, IWindowMenu &menu) {
    const auto tools = VirtualToolIndex::all().skip_all_disabled();
    const auto prev_focus = menu.focused_item_index();
    const auto restore_focus = [&] {
        // Opening a dialog with a menu moves the focus of the current menu
        menu.move_focus_to_index(prev_focus);
    };

    if (tools.at_end()) {
        return true;
    }

    // The vendor list starts at the first tool's previous vendor
    const auto vendor = filament_library_dialog::pick_vendor(_("VENDOR"), FilamentSpool::previous_for_tool(*tools).vendor);
    if (!vendor) {
        restore_focus();
        return false;
    }

    // Going back from a tool's color stops asking, and the remaining tools keep their previous colors
    bool asking = true;
    for (const auto tool : tools) {
        auto &item = config[tool];
        if (item.action != Action::change) {
            continue;
        }

        FilamentColor color = FilamentSpool::previous_for_tool(tool).color;
        if (asking) {
            if (const auto picked = filament_library_dialog::pick_color_for_tool(tool, color)) {
                color = *picked;
            } else {
                asking = false;
            }
        }
        item.spool = FilamentSpool { .vendor = *vendor, .color = color };
    }

    restore_focus();
    return true;
}

void MI_ActionSelect::printIcon(Rect16 icon_rect, ropfn raster_op, Color color_back) const {
    if (std::holds_alternative<VirtualToolIndex>(tool_filter_) && config().action == Action::change) {
        if (const auto swatch = spool_.color.color()) {
            filament_swatch::draw(icon_rect, color_back, *swatch);
            return;
        }
    }
    MenuItemSelectMenu::printIcon(icon_rect, raster_op, color_back);
}
#endif

MI_ApplyChanges::MI_ApplyChanges()
    : IWindowMenuItem(_("Carry Out the Changes"), &img::arrow_right_10x16, is_enabled_t::yes, is_hidden_t::no) {}

void MI_ApplyChanges::click(IWindowMenu &menu) {
    menu.WindowEvent(&menu, GUI_event_t::CHILD_CLICK, nullptr);
}

MenuMultiFilamentChange::MenuMultiFilamentChange(window_t *parent, const Rect16 &rect)
    : WindowMenu(parent, rect) {
    BindContainer(container);
}

MultiFilamentChangeConfig MenuMultiFilamentChange::configuration() const {
    return [&]<size_t... ix>(std::index_sequence<ix...>) {
        return MultiFilamentChangeConfig {
            ConfigItem { container.Item<WithConstructorArgs<MI_ActionSelect, ix>>().config() }...
        };
    }(std::make_index_sequence<VirtualToolIndex::count>());
}

void MenuMultiFilamentChange::set_configuration(const MultiFilamentChangeConfig &set) {
    // Set the correct indexes for the actions
    stdext::visit_sequence<VirtualToolIndex::count>([&]<size_t ix>() {
        container.Item<WithConstructorArgs<MI_ActionSelect, ix>>().set_config(set[VirtualToolIndex::from_raw(ix)]);
    });
}

void MenuMultiFilamentChange::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    switch (event) {

    case GUI_event_t::LOOP: {
        if (set_all_to_picker_pending_) {
            set_all_to_picker_pending_ = false;

            // Blocks until the user picks an item or cancels the dialog
            container.Item<WithConstructorArgs<MI_ActionSelect, MI_ActionSelect::SetAllToMode {}>>().Click(*this);

            const auto config = configuration();
            if (std::ranges::all_of(config, [](const ConfigItem &item) { return item.action == Action::keep; })) {
                // Cancelled or "Don't change" picked - nothing to apply, return to the parent menu
                Screens::Access()->Close();
                return;
            }

            // Focus apply-changes so that the selection can be confirmed right away
            move_focus_to_index(container.GetVisibleIndex(container.Item<MI_ApplyChanges>()));
        }
        break;
    }

    case GUI_event_t::CHILD_CLICK: {
        if (carry_out_changes()) {
            Screens::Access()->Close();
        }
        return;
    }

    case GUI_event_t::MEDIA: {
        const MediaState_t media_state = MediaState_t(reinterpret_cast<int>(param));
        if (media_state == MediaState_t::removed || media_state == MediaState_t::error) {
            // USB was removed
            if (close_screen_on_media_disconnect_) {
                Screens::Access()->Close();
                return;
            }
        }
        break;
    }

    default:
        break;
    }

    WindowMenu::windowEvent(sender, event, param);
}

bool MenuMultiFilamentChange::carry_out_changes() {
    auto config = configuration();
#if HAS_FILAMENT_SLOTS()
    if (!pick_missing_spools(config)) {
        return false;
    }
#endif

    // Note: This potentially duplicates warnings raised by manually changing the items
    // But the config could have been pre-filled, so better to sometimes warn twice than sometimes not at all
    if (!gui_config_confirm_incompatibilities(config, Response::Cancel)) {
        return false;
    }

    ArrayStringBuilder<MAX_CMD_SIZE> sb;
    multi_filament_change::config_to_gcode(config, sb);
    marlin_client::gcode(sb.str());
    return true;
}

#if HAS_FILAMENT_SLOTS()
bool MenuMultiFilamentChange::pick_missing_spools(MultiFilamentChangeConfig &config) {
    const auto is_missing = [](const ConfigItem &item) {
        return item.action == Action::change && item.spool == ConfigItem::unpicked_spool;
    };
    if (std::ranges::none_of(config, is_missing)) {
        return true;
    }

    const auto prev_focus = focused_item_index();
    bool picked_all = true;
    for (const auto tool : VirtualToolIndex::all().skip_all_disabled()) {
        auto &item = config[tool];
        if (!is_missing(item)) {
            continue;
        }

        const auto picked = filament_library_dialog::pick_spool_for_tool(tool, FilamentSpool::previous_for_tool(tool));
        if (!picked) {
            picked_all = false;
            break;
        }
        item.spool = *picked;
    }

    // Carrying out the changes again after going back does not ask for what was picked
    set_configuration(config);

    // Opening a dialog with a menu moves the focus of the current menu
    move_focus_to_index(prev_focus);
    return picked_all;
}
#endif

static constexpr const char *header_text = HAS_MMU2() ? N_("FILAMENT CHANGE") : N_("MULTITOOL FILAMENT CHANGE");

ScreenChangeAllFilaments::ScreenChangeAllFilaments()
    : ScreenMenuBase(nullptr, _(header_text), EFooter::On) //
{
    EnableLongHoldScreenAction();
    Screens::Access()->DisableMenuTimeout();
    menu.menu.set_configuration({});
}

ScreenChangeAllFilaments::ScreenChangeAllFilaments(SetupForPrint)
    : ScreenChangeAllFilaments {} {
    menu.menu.set_configuration(multi_filament_change::config_from_current_print_setup());
    menu.menu.close_screen_on_media_disconnect_ = true;
}

ScreenChangeAllFilaments::ScreenChangeAllFilaments(SetupUnloadAll)
    : ScreenChangeAllFilaments {} {

    multi_filament_change::Config config;
    for (auto tool : VirtualToolIndex::all().skip_all_disabled()) {
        config[tool].action = multi_filament_change::Action::unload;
    }
    menu.menu.set_configuration(config);

    // Preselect apply-changes, all should be clear
    menu.menu.move_focus_to_index(menu.menu.container.GetVisibleIndex(menu.menu.container.Item<MI_ApplyChanges>()));
}

ScreenChangeAllFilaments::ScreenChangeAllFilaments(SetupLoadAll)
    : ScreenChangeAllFilaments {} {
    // The picker dialog cannot be opened right away, the screen must become the captured window first
    menu.menu.set_all_to_picker_pending_ = true;
}
