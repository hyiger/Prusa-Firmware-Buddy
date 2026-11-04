add_executable(marlin_server_state_tests marlin_server_state_tests.cpp)
# Option stubs mirror COREONE_INDX, which has the INDX-only states
target_include_directories(
  marlin_server_state_tests PRIVATE stub_include ${CMAKE_SOURCE_DIR}/src/common/marlin_server_types
  )
add_catch_test(marlin_server_state_tests)
