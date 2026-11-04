add_executable(indx_fault_status_tests ${CMAKE_CURRENT_SOURCE_DIR}/indx_fault_status.cpp)
target_include_directories(
  indx_fault_status_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub ${CMAKE_SOURCE_DIR}/include
                                  ${CMAKE_SOURCE_DIR}/src/module/indx_head/include
  )
add_catch_test(indx_fault_status_tests)
