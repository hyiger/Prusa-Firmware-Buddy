#include "filament_library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include <config_store/store_instance.hpp>
#include <i18n.h>
#include <utils/enum_array.hpp>

namespace {

constexpr EnumArray<PresetFilamentVendor, const char *, PresetFilamentVendor::_count> preset_vendor_names {
    { PresetFilamentVendor::generic, "Generic" },
    { PresetFilamentVendor::prusament, "Prusament" },
    { PresetFilamentVendor::polymaker, "Polymaker" },
    { PresetFilamentVendor::bambu_lab, "Bambu Lab" },
    { PresetFilamentVendor::esun, "eSun" },
    { PresetFilamentVendor::elegoo, "Elegoo" },
    { PresetFilamentVendor::sunlu, "Sunlu" },
    { PresetFilamentVendor::overture, "Overture" },
    { PresetFilamentVendor::hatchbox, "Hatchbox" },
    { PresetFilamentVendor::inland, "Inland" },
    { PresetFilamentVendor::three_d_fuel, "3D-Fuel" },
    { PresetFilamentVendor::fillamentum, "Fillamentum" },
    { PresetFilamentVendor::fiberlogy, "Fiberlogy" },
    { PresetFilamentVendor::spectrum, "Spectrum" },
    { PresetFilamentVendor::extrudr, "Extrudr" },
    { PresetFilamentVendor::colorfabb, "colorFabb" },
};

struct PresetColorInfo {
    const char *name;
    Color color;
};

// CSS named color values, except brown (CSS saddlebrown, CSS brown reads as maroon) and natural (CSS beige)
constexpr EnumArray<PresetFilamentColor, PresetColorInfo, PresetFilamentColor::_count> preset_colors {
    { PresetFilamentColor::red, { "Red", Color::from_raw(0xFF0000) } },
    { PresetFilamentColor::green, { "Green", Color::from_raw(0x008000) } },
    { PresetFilamentColor::blue, { "Blue", Color::from_raw(0x0000FF) } },
    { PresetFilamentColor::cyan, { "Cyan", Color::from_raw(0x00FFFF) } },
    { PresetFilamentColor::magenta, { "Magenta", Color::from_raw(0xFF00FF) } },
    { PresetFilamentColor::yellow, { "Yellow", Color::from_raw(0xFFFF00) } },
    { PresetFilamentColor::black, { "Black", Color::from_raw(0x000000) } },
    { PresetFilamentColor::white, { "White", Color::from_raw(0xFFFFFF) } },
    { PresetFilamentColor::orange, { "Orange", Color::from_raw(0xFFA500) } },
    { PresetFilamentColor::brown, { "Brown", Color::from_raw(0x8B4513) } },
    { PresetFilamentColor::gray, { "Gray", Color::from_raw(0x808080) } },
    { PresetFilamentColor::purple, { "Purple", Color::from_raw(0x800080) } },
    { PresetFilamentColor::pink, { "Pink", Color::from_raw(0xFFC0CB) } },
    { PresetFilamentColor::silver, { "Silver", Color::from_raw(0xC0C0C0) } },
    { PresetFilamentColor::natural, { "Natural", Color::from_raw(0xF5F5DC) } },
    { PresetFilamentColor::gold, { "Gold", Color::from_raw(0xFFD700) } },
};

// The Preset parameters below only select the library

const char *preset_name(PresetFilamentVendor preset) {
    return preset_vendor_names[preset];
}

const char *preset_name(PresetFilamentColor preset) {
    return preset_colors[preset].name;
}

FilamentLibraryName user_name(PresetFilamentVendor, uint8_t index) {
    return config_store().user_filament_vendors.get(index);
}

FilamentLibraryName user_name(PresetFilamentColor, uint8_t index) {
    return config_store().user_filament_colors.get(index).name;
}

void set_user_name(PresetFilamentVendor, uint8_t index, const FilamentLibraryName &name) {
    config_store().user_filament_vendors.set(index, name);
}

void set_user_name(PresetFilamentColor, uint8_t index, const FilamentLibraryName &name) {
    config_store().user_filament_colors.transform(index, [&](UserFilamentColor_EEPROM value) {
        value.name = name;
        return value;
    });
}

auto &visibility_store_item(PresetFilamentVendor) {
    return config_store().visible_filament_vendors;
}

auto &visibility_store_item(PresetFilamentColor) {
    return config_store().visible_filament_colors;
}

bool equals_ignore_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() && strncasecmp(a.data(), b.data(), a.size()) == 0;
}

bool is_valid_name_char(char ch) {
    return isalnum(static_cast<unsigned char>(ch)) || strchr(" _-.+&", ch);
}

template <typename Item>
std::optional<uint8_t> visibility_bit(Item item) {
    if (const auto preset = item.preset_value()) {
        return std::to_underlying(*preset);
    } else if (const auto index = item.user_index()) {
        return Item::user_visibility_bit_offset + *index;
    } else {
        return std::nullopt;
    }
}

} // namespace

template <typename Preset, uint8_t user_count>
FilamentLibraryItem<Preset, user_count> FilamentLibraryItem<Preset, user_count>::from_name(std::string_view name) {
    for (const auto item : all()) {
        if (equals_ignore_case(item.name(), name)) {
            return item;
        }
    }
    return {};
}

template <typename Preset, uint8_t user_count>
FilamentLibraryName FilamentLibraryItem<Preset, user_count>::name() const {
    if (const auto preset = preset_value()) {
        return preset_name(*preset);
    } else if (const auto index = user_index()) {
        return user_name(Preset {}, *index);
    } else {
        return {};
    }
}

template <typename Preset, uint8_t user_count>
bool FilamentLibraryItem<Preset, user_count>::is_visible() const {
    const auto bit = visibility_bit(*this);
    return bit && visibility_store_item(Preset {}).get().test(*bit);
}

template <typename Preset, uint8_t user_count>
void FilamentLibraryItem<Preset, user_count>::set_visible(bool set) const {
    if (const auto bit = visibility_bit(*this)) {
        visibility_store_item(Preset {}).apply([&](auto &value) {
            value.set(*bit, set);
        });
    }
}

template <typename Preset, uint8_t user_count>
std::expected<void, const char *> FilamentLibraryItem<Preset, user_count>::can_be_renamed_to(std::string_view new_name) const {
    if (!user_index()) {
        return std::unexpected(N_("Only user entries can be renamed"));
    }

    if (new_name.empty()) {
        return std::unexpected(N_("Name must not be empty"));
    }

    if (new_name.size() >= filament_library_name_buffer_size) {
        return std::unexpected(N_("Name is too long"));
    }

    if (new_name.front() == ' ' || new_name.back() == ' ' || !std::ranges::all_of(new_name, is_valid_name_char)) {
        return std::unexpected(N_("Name must contain only 'a-zA-Z0-9 _-.+&' characters"));
    }

    for (const auto item : all()) {
        if (item != *this && equals_ignore_case(item.name(), new_name)) {
            return std::unexpected(N_("Name is already used"));
        }
    }

    return {};
}

template <typename Preset, uint8_t user_count>
void FilamentLibraryItem<Preset, user_count>::set_name(std::string_view new_name) const {
    if (!can_be_renamed_to(new_name)) {
        return;
    }
    set_user_name(Preset {}, *user_index(), FilamentLibraryName(new_name));
}

template <typename Preset, uint8_t user_count>
std::optional<Color> FilamentLibraryItem<Preset, user_count>::color() const
    requires std::is_same_v<Preset, PresetFilamentColor>
{
    if (const auto preset = preset_value()) {
        return preset_colors[*preset].color;
    } else if (const auto index = user_index()) {
        const auto stored = config_store().user_filament_colors.get(*index);
        return Color::from_rgb(stored.r, stored.g, stored.b);
    } else {
        return std::nullopt;
    }
}

template <typename Preset, uint8_t user_count>
void FilamentLibraryItem<Preset, user_count>::set_color(Color color) const
    requires std::is_same_v<Preset, PresetFilamentColor>
{
    if (const auto index = user_index()) {
        config_store().user_filament_colors.transform(*index, [&](UserFilamentColor_EEPROM value) {
            value.r = color.r;
            value.g = color.g;
            value.b = color.b;
            return value;
        });
    }
}

template struct FilamentLibraryItem<PresetFilamentVendor, user_filament_vendor_count>;
template struct FilamentLibraryItem<PresetFilamentColor, user_filament_color_count>;

static_assert(FilamentColor::from_data(FilamentColor::preset(PresetFilamentColor::gold).data).preset_value() == PresetFilamentColor::gold);
static_assert(FilamentVendor::from_data(FilamentVendor::user(user_filament_vendor_count - 1).data).user_index() == user_filament_vendor_count - 1);
static_assert(!FilamentVendor::from_data(FilamentVendor::preset_count + 1));
static_assert(!FilamentVendor::from_data(FilamentVendor::user_data_offset + user_filament_vendor_count));
static_assert(!FilamentSpool {}.vendor && !FilamentSpool {}.color);
static_assert(FilamentVendor::all()[FilamentVendor::preset_count] == FilamentVendor::user(0));

FilamentSpool FilamentSpool::for_tool(VirtualToolIndex tool) {
    const auto stored = config_store().loaded_filament_spool.get(tool.to_raw());
    return FilamentSpool {
        .vendor = FilamentVendor::from_data(stored.vendor.data),
        .color = FilamentColor::from_data(stored.color.data),
    };
}

void FilamentSpool::set_for_tool(VirtualToolIndex tool, FilamentSpool spool) {
    config_store().loaded_filament_spool.set(tool.to_raw(), spool);
}

ColorHexString color_to_hex(Color color) {
    ColorHexString result;
    snprintf(result.data(), result.size(), "#%02X%02X%02X", color.r, color.g, color.b);
    return result;
}
