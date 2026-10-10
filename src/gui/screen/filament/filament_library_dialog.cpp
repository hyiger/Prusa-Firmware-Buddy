#include "filament_library_dialog.hpp"
#include "filament_swatch.hpp"

#include <type_traits>

#include <algorithm_extensions.hpp>
#include <gui/ScreenHandler.hpp>
#include <IDialog.hpp>
#include <inplace_vector.hpp>
#include <window_header.hpp>
#include <window_menu_adv.hpp>
#include <window_menu_virtual.hpp>
#include <WindowMenuItems.hpp>

namespace filament_library_dialog {

namespace {

    template <typename Item>
    class DialogItem final : public IWindowMenuItem {
    public:
        DialogItem(Item item);

    protected:
        void click(IWindowMenu &menu) override;
        void printIcon(Rect16 icon_rect, ropfn raster_op, Color color_back) const override;

    private:
        Item item_;
        FilamentLibraryName name_;
        std::optional<Color> swatch_;
    };

    // The dialog lives on the stack, so the menu only reserves room for its own items
    template <typename Item>
    class DialogMenu final : public WindowMenuVirtualSized<WindowMenuVirtualBase::default_item_buffer_size, sizeof(DialogItem<Item>)> {
    public:
        DialogMenu(window_t *parent, Rect16 rect, Item current);

        int item_count() const final {
            return items_.size() + 1;
        }

        std::optional<Item> result;

    protected:
        void setup_item(typename DialogMenu::ItemVariant &variant, int index) final;

    private:
        /// None, then the visible items, plus the current one if it is hidden
        stdext::inplace_vector<Item, Item::total_count + 1> items_;
    };

    template <typename Item>
    class Dialog final : public IDialog {
    public:
        Dialog(const string_view_utf8 &title, Item current)
            : IDialog(GuiDefaults::RectScreen)
            , header_(this, title)
            , menu_(this, GuiDefaults::RectScreenNoHeader, current) {
            CaptureNormalWindow(menu_);
        }

        std::optional<Item> result() const {
            return menu_.menu.result;
        }

    private:
        window_header_t header_;
        WindowExtendedMenu<DialogMenu<Item>> menu_;
    };

    template <typename Item>
    DialogItem<Item>::DialogItem(Item item)
        : IWindowMenuItem({}, nullptr)
        , item_(item)
        , name_(item.name()) {
        if constexpr (std::is_same_v<Item, FilamentColor>) {
            swatch_ = item.color();
        }
        SetLabel(item ? string_view_utf8::MakeRAM(name_.data()) : _("None"));
    }

    template <typename Item>
    void DialogItem<Item>::click(IWindowMenu &menu) {
        static_cast<DialogMenu<Item> &>(menu).result = item_;
        Screens::Access()->Close();
    }

    template <typename Item>
    void DialogItem<Item>::printIcon(Rect16 icon_rect, [[maybe_unused]] ropfn raster_op, Color color_back) const {
        if (swatch_) {
            filament_swatch::draw(icon_rect, color_back, *swatch_);
        }
    }

    template <typename Item>
    DialogMenu<Item>::DialogMenu(window_t *parent, Rect16 rect, Item current)
        : WindowMenuVirtualSized<WindowMenuVirtualBase::default_item_buffer_size, sizeof(DialogItem<Item>)>(parent, rect, WindowMenuVirtualBase::CloseScreenReturnBehavior::yes) {
        items_.push_back({});
        for (const auto item : Item::all()) {
            if (item.is_visible() || item == current) {
                items_.push_back(item);
            }
        }
        this->setup_items();

        // +1 for the return item
        this->move_focus_to_index(stdext::index_of(items_, current) + 1);
    }

    template <typename Item>
    void DialogMenu<Item>::setup_item(typename DialogMenu::ItemVariant &variant, int index) {
        if (index == 0) {
            variant.template emplace<MI_RETURN>();
        } else {
            variant.template emplace<DialogItem<Item>>(items_[index - 1]);
        }
    }

    template <typename Item>
    std::optional<Item> pick(const string_view_utf8 &title, Item current) {
        Dialog<Item> dialog(title, current);
        Screens::Access()->gui_loop_until_dialog_closed();
        return dialog.result();
    }

} // namespace

std::optional<FilamentVendor> pick_vendor(const string_view_utf8 &title, FilamentVendor current) {
    return pick(title, current);
}

std::optional<FilamentColor> pick_color(const string_view_utf8 &title, FilamentColor current) {
    return pick(title, current);
}

bool pick_spool_for_load(FilamentType type, std::variant<VirtualToolIndex, AllTools> tool, PreheatMode mode) {
    switch (mode) {

    case PreheatMode::standard_load:
    case PreheatMode::change_load:
    case PreheatMode::autoload:
        break;

    case PreheatMode::preheat:
    case PreheatMode::unload:
    case PreheatMode::purge:
        return true;
    }

    const auto *virtual_tool = std::get_if<VirtualToolIndex>(&tool);
    if (!virtual_tool) {
        return true;
    }

    FilamentSpool spool = FilamentSpool::previous_for_tool(*virtual_tool);
    while (true) {
        const auto vendor = pick_vendor(_("VENDOR"), spool.vendor);
        if (!vendor) {
            return false;
        }
        spool.vendor = *vendor;

        // Going back from the color returns to the vendor
        if (const auto color = pick_color(_("COLOR"), spool.color)) {
            spool.color = *color;
            FilamentSpool::pick_for_load(*virtual_tool, type, spool);
            return true;
        }
    }
}

} // namespace filament_library_dialog
