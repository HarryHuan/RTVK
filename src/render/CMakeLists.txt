set(CMAKE_AUTOMOC ON)

add_library(rtvk_render STATIC
    vulkan_window.h
    vulkan_window.cpp
    pipeline_builder.cpp
    material_manager.cpp
    post_processor.cpp
)
target_link_libraries(rtvk_render PUBLIC
    rtvk_core
    Qt6::Core Qt6::Gui
    Vulkan::Vulkan
)
target_include_directories(rtvk_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
