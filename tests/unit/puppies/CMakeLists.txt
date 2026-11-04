add_subdirectory(cyphal_node_stubs)
add_subdirectory(indx_fault_status)
add_subdirectory(minimal_cyphal_node)

add_executable(
  time_sync ${CMAKE_CURRENT_SOURCE_DIR}/time_sync.cpp ${CMAKE_SOURCE_DIR}/src/puppies/time_sync.cpp
  )

target_include_directories(
  time_sync PUBLIC ${CMAKE_SOURCE_DIR}/tests/stubs ${CMAKE_SOURCE_DIR}/include
                   ${CMAKE_SOURCE_DIR}/src/common
  )

target_link_libraries(time_sync SG14 buddy_utils)

add_catch_test(time_sync)

add_executable(nozzle_presence_cache ${CMAKE_CURRENT_SOURCE_DIR}/nozzle_presence_cache.cpp)
target_include_directories(
  nozzle_presence_cache PUBLIC ${CMAKE_SOURCE_DIR}/include
                               ${CMAKE_SOURCE_DIR}/src/module/indx_head/include
  )
add_catch_test(nozzle_presence_cache)
