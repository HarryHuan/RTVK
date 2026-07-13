add_library(rtvk_core STATIC
    config.h
    config.cpp
    math_common.h
    math_common.cpp
)
target_link_libraries(rtvk_core PUBLIC Qt6::Core)
target_include_directories(rtvk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
