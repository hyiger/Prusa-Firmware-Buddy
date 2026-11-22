add_executable(
  pressure_advance_estimator_tests
  ${CMAKE_SOURCE_DIR}/src/feature/pressure_advance_calibration/pressure_advance_estimator.cpp
  pressure_advance_estimator_tests.cpp
  )
target_include_directories(pressure_advance_estimator_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_catch_test(pressure_advance_estimator_tests)
