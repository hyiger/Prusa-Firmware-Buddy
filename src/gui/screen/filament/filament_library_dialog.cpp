#include "filament_library_dialog.hpp"
#include "filament_swatch.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <type_traits>

#include <algorithm_extensions.hpp>
#include <gui/ScreenHandler.hpp>
#include <IDialog.hpp>
#include <inplace_vector.hpp>
#include <utils/string_builder.hpp>
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

namespace {

    /// Dialog title naming a tool, from a translated \p format with one %s for the tool's name.
    /// The text refers to the object's own buffer, so it cannot be copied.
    class ToolTitle {
    public:
        ToolTitle(VirtualToolIndex tool, const char *format) {
            VirtualToolIndex::DisplayNameParams name_params;
            std::array<char, 24> name {};
            StringBuilder(name).append_string_view(tool.display_name(name_params));
            text_ = _(format).formatted(params_, name.data());

            // The parameters are kept in the buffer, so this upper-cases the tool's name
            uppercase_title(params_.buffer);
        }

        ToolTitle(const ToolTitle &) = delete;
        ToolTitle &operator=(const ToolTitle &) = delete;

        const string_view_utf8 &text() const {
            return text_;
        }

    private:
        StringViewUtf8Parameters<24> params_;
        string_view_utf8 text_;
    };

    std::optional<FilamentSpool> pick_spool(const string_view_utf8 &vendor_title, const string_view_utf8 &color_title, FilamentSpool initial) {
        FilamentSpool spool = initial;
        while (true) {
            const auto vendor = pick_vendor(vendor_title, spool.vendor);
            if (!vendor) {
                return std::nullopt;
            }
            spool.vendor = *vendor;

            if (const auto color = pick_color(color_title, spool.color)) {
                spool.color = *color;
                return spool;
            }
        }
    }

} // namespace

std::optional<FilamentColor> pick_color_for_tool(VirtualToolIndex tool, FilamentColor current) {
    const ToolTitle title(tool, N_("%s COLOR"));
    return pick_color(title.text(), current);
}

std::optional<FilamentSpool> pick_spool(FilamentSpool initial) {
    return pick_spool(_("VENDOR"), _("COLOR"), initial);
}

std::optional<FilamentSpool> pick_spool_for_tool(VirtualToolIndex tool, FilamentSpool initial) {
    const ToolTitle vendor_title(tool, N_("%s VENDOR"));
    const ToolTitle color_title(tool, N_("%s COLOR"));
    return pick_spool(vendor_title.text(), color_title.text(), initial);
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

    const auto spool = pick_spool(FilamentSpool::previous_for_tool(*virtual_tool));
    if (!spool) {
        return false;
    }
    FilamentSpool::pick_for_load(*virtual_tool, type, *spool);
    return true;
}

void uppercase_title(std::span<char> text) {
    const std::string_view view(text.data(), strnlen(text.data(), text.size()));
    if (!std::ranges::all_of(view, [](char ch) { return static_cast<unsigned char>(ch) < 0x80; })) {
        return;
    }
    std::ranges::transform(text, text.begin(), [](char ch) { return static_cast<char>(toupper(ch)); });
}

} // namespace filament_library_dialog
