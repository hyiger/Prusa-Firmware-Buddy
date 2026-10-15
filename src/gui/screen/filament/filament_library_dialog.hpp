/// @file
/// Dialogs for picking a filament vendor or color, for loading and for the Filament Slots screens
#pragma once

#include <optional>
#include <span>
#include <variant>

#include <client_fsm_types.hpp>
#include <filament.hpp>
#include <filament_library.hpp>
#include <string_view_utf8.hpp>
#include <tool_index.hpp>

namespace filament_library_dialog {

/// Lets the user pick None or a vendor: the visible ones, plus \p current if it is hidden.
/// Blocks until the user picks. \returns std::nullopt if the user went back.
std::optional<FilamentVendor> pick_vendor(const string_view_utf8 &title, FilamentVendor current);

/// Lets the user pick None or a color, each shown with its swatch, like pick_vendor
std::optional<FilamentColor> pick_color(const string_view_utf8 &title, FilamentColor current);

/// Picked to leave a tool out
struct DontLoad {};

using ColorOrDontLoad = std::variant<FilamentColor, DontLoad>;

/// Like pick_color, for one of several tools to be loaded: titled with the name of \p tool, and with
/// "Don't load" at the top for leaving the tool out
std::optional<ColorOrDontLoad> pick_color_for_tool(VirtualToolIndex tool, FilamentColor current);

/// Asks for a vendor and then a color, starting at \p initial. Going back from the color returns to the vendor.
/// \returns std::nullopt if the user went back from the vendor
std::optional<FilamentSpool> pick_spool(FilamentSpool initial);

/// Like pick_spool, in dialogs titled with the name of \p tool
std::optional<FilamentSpool> pick_spool_for_tool(VirtualToolIndex tool, FilamentSpool initial);

/// Upper-cases the NUL-terminated \p text in place, as screen titles are, unless it has non-ASCII letters,
/// which would end up only partly upper-cased
void uppercase_title(std::span<char> text);

/// Asks for the vendor and color of the filament about to be loaded, once its type is known.
/// Applies to loads into a single tool. The pickers start at the tool's previous spool.
/// \returns false if the user went back from the vendor, as to picking the type
bool pick_spool_for_load(FilamentType type, std::variant<VirtualToolIndex, AllTools> tool, PreheatMode mode);

} // namespace filament_library_dialog
