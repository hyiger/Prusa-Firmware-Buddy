/// @file
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include <color.hpp>
#include <tool_index.hpp>
#include <utils/string/inplace_string.hpp>

#include <option/has_filament_slots.h>
static_assert(HAS_FILAMENT_SLOTS());

/// Size of a filament vendor or color name buffer, including the terminating zero
/// !!! DO NOT CHANGE - this is used in config store
constexpr size_t filament_library_name_buffer_size = 16;

using FilamentLibraryName = InplaceString<filament_library_name_buffer_size>;

/// !!! DO NOT REORDER, DO NOT CHANGE - this is used in config store
enum class PresetFilamentVendor : uint8_t {
    generic = 0,
    prusament = 1,
    polymaker = 2,
    bambu_lab = 3,
    esun = 4,
    elegoo = 5,
    sunlu = 6,
    overture = 7,
    hatchbox = 8,
    inland = 9,
    three_d_fuel = 10,
    fillamentum = 11,
    fiberlogy = 12,
    spectrum = 13,
    extrudr = 14,
    colorfabb = 15,
    _count
};

/// !!! DO NOT REORDER, DO NOT CHANGE - this is used in config store
enum class PresetFilamentColor : uint8_t {
    red = 0,
    green = 1,
    blue = 2,
    cyan = 3,
    magenta = 4,
    yellow = 5,
    black = 6,
    white = 7,
    orange = 8,
    brown = 9,
    gray = 10,
    purple = 11,
    pink = 12,
    silver = 13,
    natural = 14,
    gold = 15,
    _count
};

/// Number of user-definable vendors.
/// Can be raised up to the max_item_count of the config store item, at the cost of EEPROM space.
constexpr uint8_t user_filament_vendor_count = 8;

/// Number of user-definable colors.
/// Can be raised up to the max_item_count of the config store item, at the cost of EEPROM space.
constexpr uint8_t user_filament_color_count = 8;

/// An entry of the filament vendor or color library: none, a preset, or a user slot.
/// Presets are built in. User slots are fixed in number and renamed by the user, like user filament types.
/// Both kinds can be hidden from the selection lists.
template <typename Preset, uint8_t user_count_>
struct FilamentLibraryItem {

public:
    static constexpr uint8_t preset_count = std::to_underlying(Preset::_count);
    static constexpr uint8_t user_count = user_count_;
    static constexpr size_t total_count = preset_count + user_count;

    /// Visibility is a 32-bit bitset: presets from bit 0, user slots from this bit
    static constexpr uint8_t user_visibility_bit_offset = 24;
    static_assert(preset_count <= user_visibility_bit_offset);
    static_assert(user_visibility_bit_offset + user_count <= 32);

    static constexpr uint8_t user_data_offset = 128;
    static_assert(preset_count < user_data_offset);

    /// 0 for none, 1 + preset, or user_data_offset + user slot index.
    /// Public only because the type is used as a non-type template parameter. Use the accessors.
    uint8_t data = 0;

public:
    static constexpr FilamentLibraryItem preset(Preset preset) {
        return { static_cast<uint8_t>(std::to_underlying(preset) + 1) };
    }

    static constexpr FilamentLibraryItem user(uint8_t index) {
        return { static_cast<uint8_t>(user_data_offset + index) };
    }

    /// \returns the item encoded as \p data, or none if \p data does not encode a valid item
    static constexpr FilamentLibraryItem from_data(uint8_t data) {
        const FilamentLibraryItem result { data };
        return result ? result : FilamentLibraryItem {};
    }

    /// \returns all items: presets first, then user slots
    static constexpr std::array<FilamentLibraryItem, total_count> all() {
        std::array<FilamentLibraryItem, total_count> result;
        for (uint8_t i = 0; i < preset_count; i++) {
            result[i] = preset(static_cast<Preset>(i));
        }
        for (uint8_t i = 0; i < user_count; i++) {
            result[preset_count + i] = user(i);
        }
        return result;
    }

    /// \returns the item named \p name (case insensitive), or none
    static FilamentLibraryItem from_name(std::string_view name);

public:
    constexpr std::optional<Preset> preset_value() const {
        if (data >= 1 && data <= preset_count) {
            return static_cast<Preset>(data - 1);
        }
        return std::nullopt;
    }

    constexpr std::optional<uint8_t> user_index() const {
        if (data >= user_data_offset && data < user_data_offset + user_count) {
            return data - user_data_offset;
        }
        return std::nullopt;
    }

    /// \returns whether the item is a valid preset or user slot
    constexpr explicit operator bool() const {
        return preset_value().has_value() || user_index().has_value();
    }

    /// \returns name of the item, empty for none
    FilamentLibraryName name() const;

    /// \returns whether the item is shown in the selection lists
    bool is_visible() const;

    void set_visible(bool set) const;

    /// \returns whether the item can be renamed to \p new_name, or a translatable error.
    /// Only user slots can be renamed, and names must be unique within the library.
    std::expected<void, const char *> can_be_renamed_to(std::string_view new_name) const;

    /// Renames a user slot. \p new_name must pass can_be_renamed_to.
    void set_name(std::string_view new_name) const;

    /// \returns the color, std::nullopt for none
    std::optional<Color> color() const
        requires std::is_same_v<Preset, PresetFilamentColor>;

    /// Changes the color of a user slot. Does nothing for presets.
    void set_color(Color color) const
        requires std::is_same_v<Preset, PresetFilamentColor>;

public:
    constexpr bool operator==(const FilamentLibraryItem &) const = default;
    constexpr bool operator!=(const FilamentLibraryItem &) const = default;
};

using FilamentVendor = FilamentLibraryItem<PresetFilamentVendor, user_filament_vendor_count>;
using FilamentColor = FilamentLibraryItem<PresetFilamentColor, user_filament_color_count>;

extern template struct FilamentLibraryItem<PresetFilamentVendor, user_filament_vendor_count>;
extern template struct FilamentLibraryItem<PresetFilamentColor, user_filament_color_count>;

/// Vendor and color of the filament loaded in a tool.
/// The filament type lives separately, in the loaded filament type.
/// !!! DO NOT CHANGE - this is used in config store
struct FilamentSpool {
    FilamentVendor vendor;
    FilamentColor color;

    /// \returns vendor and color assigned to \p tool.
    /// They are cleared whenever the filament is unloaded from the tool.
    static FilamentSpool for_tool(VirtualToolIndex tool);

    static void set_for_tool(VirtualToolIndex tool, FilamentSpool spool);

    constexpr bool operator==(const FilamentSpool &) const = default;
    constexpr bool operator!=(const FilamentSpool &) const = default;
};
static_assert(sizeof(FilamentSpool) == 2);

/// User color slot, as stored in the config store
/// !!! DO NOT CHANGE - this is used in config store
struct UserFilamentColor_EEPROM {
    FilamentLibraryName name;
    uint8_t r = 0x80;
    uint8_t g = 0x80;
    uint8_t b = 0x80;

    constexpr bool operator==(const UserFilamentColor_EEPROM &) const = default;
};
static_assert(sizeof(UserFilamentColor_EEPROM) == 19);

/// "#RRGGBB" with the terminating zero
using ColorHexString = std::array<char, 8>;

ColorHexString color_to_hex(Color color);

/// Parses "#RRGGBB" or "RRGGBB", in either case
std::optional<Color> color_from_hex(std::string_view hex);
