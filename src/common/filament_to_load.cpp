#include "filament_to_load.hpp"

#include <option/has_filament_slots.h>
#if HAS_FILAMENT_SLOTS()
    #include <filament_library.hpp>
#endif

static FilamentType filament_to_load = FilamentType::none;

FilamentType filament::get_type_to_load() {
    return filament_to_load;
}

void filament::set_type_to_load(FilamentType filament) {
    filament_to_load = filament;
#if HAS_FILAMENT_SLOTS()
    FilamentSpool::start_load(filament);
#endif
}

void filament::update_type_to_load(FilamentType filament) {
    filament_to_load = filament;
}

FilamentType filament::apply_load_color(FilamentType filament, VirtualToolIndex virtual_tool, OptionalColor color) {
    // NoFilament must stay NoFilament
    if (!color.has_value() || filament == FilamentType::none) {
        return filament;
    }

    FilamentTypeParameters params = filament.parameters();
    params.color = *color;

    // virtual_tool will matter once pending adhoc filaments become per-tool
    (void)virtual_tool;

    // Pending adhoc -> the color applies to this load only,
    // the user preset stays untouched (and a system preset cannot be changed at all)
    filament = FilamentType::pending_adhoc;
    filament.set_parameters(params);
    return filament;
}
