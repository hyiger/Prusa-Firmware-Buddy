target_sources(
  firmware
  PRIVATE screen_filament_detail.cpp screen_filament_management.cpp
          screen_filament_management_list.cpp screen_filaments_loaded.cpp
          screen_filaments_reorder.cpp screen_filaments_visibility.cpp
  )

if(HAS_FILAMENT_SLOTS)
  target_sources(
    firmware PRIVATE filament_swatch.cpp screen_filament_library.cpp screen_filament_slots.cpp
    )
endif()
