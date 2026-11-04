# The header is copied so that its quoted includes resolve to the stubs instead of the Marlin-heavy
# headers next to it in src/common.
configure_file(
  ${CMAKE_SOURCE_DIR}/src/common/serial_printing.hpp
  ${CMAKE_CURRENT_BINARY_DIR}/include/serial_printing.hpp COPYONLY
  )

add_executable(
  serial_printing_tests ${CMAKE_SOURCE_DIR}/src/common/serial_printing.cpp
                        serial_printing_tests.cpp
  )
target_include_directories(
  serial_printing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub
                                ${CMAKE_CURRENT_BINARY_DIR}/include
  )
target_link_libraries(serial_printing_tests options)
add_catch_test(serial_printing_tests)
