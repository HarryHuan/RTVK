add_library(rtvk_sim STATIC
    pbd_solver.h
    pbd_solver.cpp
    collision_detector.cpp
    spatial_hash.cpp
)
target_link_libraries(rtvk_sim PUBLIC rtvk_core Vulkan::Vulkan)
target_include_directories(rtvk_sim PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
