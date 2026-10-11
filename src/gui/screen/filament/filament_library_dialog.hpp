/// @file
/// Dialogs for picking a filament vendor or color, for loading and for the Filament Slots screens
#pragma once

#include <optional>
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

/// Asks for the vendor and color of the filament about to be loaded, once its type is picked.
/// Applies to loads into a single tool. The pickers start at the tool's previous spool.
/// \returns false if the user went back to picking the type
bool pick_spool_for_load(FilamentType type, std::variant<VirtualToolIndex, AllTools> tool, PreheatMode mode);

} // namespace filament_library_dialog
