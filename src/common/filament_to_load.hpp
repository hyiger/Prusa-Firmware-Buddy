#pragma once

#include <filament.hpp>

// TODO: Remove this ugly interface
namespace filament {

FilamentType get_type_to_load();

/// Starts a load, or another operation that feeds filament, of @p filament
void set_type_to_load(FilamentType filament);

/// Changes the type of the operation in progress, without starting a new one.
/// For a type that resolves while loading, such as a pending ad-hoc filament becoming the tool's own.
void update_type_to_load(FilamentType filament);

/// Returns @p filament with @p color applied for a single load.
/// The result is a pending ad-hoc type, so the (possibly system) preset is left untouched.
/// Without a color, @p filament is returned unchanged.
FilamentType apply_load_color(FilamentType filament, VirtualToolIndex virtual_tool, OptionalColor color);

} // namespace filament
