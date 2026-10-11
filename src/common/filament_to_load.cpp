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

static std::optional<Color> color_to_load { std::nullopt };

std::optional<Color> filament::get_color_to_load() {
    return color_to_load;
}

void filament::set_color_to_load(std::optional<Color> color) {
    color_to_load = color;
}
