add_executable(ubl_g29_phases_tests ubl_g29_phases_tests.cpp)
target_include_directories(
  ubl_g29_phases_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src/feature/bedlevel/ubl
  )
add_catch_test(ubl_g29_phases_tests)
