#pragma once

#include <MItem_menus.hpp>
#include <WinMenuContainer.hpp>
#include <window_menu_adv.hpp>
#include <screen_menu.hpp>

#include <filament_list.hpp>
#include <i18n.h>
#include <dynamic_index_mapping.hpp>
#include <meta_utils.hpp>
#include <multi_filament_change.hpp>
#include <gui/menu_item/menu_item_select_menu.hpp>
#include <utils/compact_optional.hpp>

class ScreenChangeAllFilaments;

namespace multi_filament_change {

class MI_ActionSelect : public MenuItemSelectMenu {

public:
    MI_ActionSelect(uint8_t tool_ix);

    struct SetAllToMode {};
    MI_ActionSelect(SetAllToMode);

    ConfigItem config() const {
        return config(current_item());
    }
    ConfigItem config(int item_index) const;
    void set_config(const ConfigItem &set);

    int item_count() const final;
    string_view_utf8 build_item_text(int index, ItemTextParams &params) const final;

private:
    static constexpr auto items = std::to_array<DynamicIndexMappingRecord<Action>>({
        Action::keep,
        Action::unload,
        { Action::change, DynamicIndexMappingType::dynamic_section },
    });

    bool on_item_selected(const OnItemSelectedArgs &args) override;

#if HAS_FILAMENT_SLOTS()
    void click(IWindowMenu &menu) override;
    void on_item_reselected(IWindowMenu &menu) override;
    int submenu_focus_item() override;
    void printIcon(Rect16 icon_rect, ropfn raster_op, Color color_back) const override;

    /// Asks for the vendor and color of the tool's new filament, starting at what was picked before.
    /// \returns false if the user went back
    bool pick_spool(IWindowMenu &menu);

    /// Asks for a vendor once, then for the color of each tool in \p config that gets new filament.
    /// "Don't load" leaves a tool out, and going back from a tool's color leaves it and the remaining
    /// tools out. Going back before any tool got a color returns to the vendor.
    /// \returns false if the user went back from the vendor
    static bool pick_spools_for_all(Config &config, IWindowMenu &menu);
#endif

private:
    CompactOptional<Color, COLOR_NONE> color;

#if HAS_FILAMENT_SLOTS()
    FilamentSpool spool_ = ConfigItem::unpicked_spool;

    /// Item to reopen the list at, after the user went back from picking its vendor and color
    std::optional<int> reopen_at_;
#endif

    StringViewUtf8Parameters<2> label_params;
    DynamicIndexMapping<items> index_mapping;
    FilamentList filament_list;

    /// Tool(s) the filament list is filtered for (hotend compatibility):
    /// AllTools for the "Set All To" item, a specific tool otherwise.
    const std::variant<VirtualToolIndex, AllTools> tool_filter_;

    bool has_filament_loaded : 1 = false;
};

class MI_ApplyChanges : public IWindowMenuItem {
public:
    MI_ApplyChanges();

protected:
    virtual void click(IWindowMenu &) override;
};

template <typename>
struct MenuMultiFilamentChange__;

template <size_t... ix>
struct MenuMultiFilamentChange__<std::index_sequence<ix...>> {
    using Container = WinMenuContainer<MI_RETURN,
        WithConstructorArgs<MI_ActionSelect, MI_ActionSelect::SetAllToMode {}>,
        WithConstructorArgs<MI_ActionSelect, ix>...,
        MI_ApplyChanges>;
};

using MenuMultiFilamentChange_ = MenuMultiFilamentChange__<std::make_index_sequence<VirtualToolIndex::count>>;

class MenuMultiFilamentChange : public WindowMenu {
    friend class ::ScreenChangeAllFilaments;

public:
    MenuMultiFilamentChange(window_t *parent, const Rect16 &rect);

public:
    MultiFilamentChangeConfig configuration() const;
    void set_configuration(const MultiFilamentChangeConfig &set);

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    [[nodiscard]] bool carry_out_changes();

#if HAS_FILAMENT_SLOTS()
    /// Asks for the vendor and color of each tool in \p config to be loaded without them yet,
    /// such as when the changes come from the print. Keeps what was picked in the menu.
    /// \returns false if the user went back
    [[nodiscard]] bool pick_missing_spools(MultiFilamentChangeConfig &config);
#endif

private:
    MenuMultiFilamentChange_::Container container;
    bool close_screen_on_media_disconnect_ = false;

    /// Open the "Set All To" picker on the next LOOP event (deferred from ScreenChangeAllFilaments(SetupLoadAll))
    bool set_all_to_picker_pending_ = false;
};

} // namespace multi_filament_change

/**
 * @brief Change filament in all tools.
 */
class ScreenChangeAllFilaments : public ScreenMenuBase<multi_filament_change::MenuMultiFilamentChange> {
public:
    ScreenChangeAllFilaments();

    struct SetupForPrint {};
    ScreenChangeAllFilaments(SetupForPrint);

    struct SetupUnloadAll {};
    ScreenChangeAllFilaments(SetupUnloadAll);

    struct SetupLoadAll {};
    ScreenChangeAllFilaments(SetupLoadAll);
};
