cmake_minimum_required(VERSION 3.25)
project(RTVK VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Vulkan REQUIRED)

# SPIR-V compilation
set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(GLOB SHADER_SOURCES "shaders/common/*" "shaders/sim/*" "shaders/render/*" "shaders/particle/*")
set(ALL_SPV_FILES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    get_filename_component(SHADER_DIR ${SHADER} DIRECTORY)
    get_filename_component(SHADER_SUBDIR ${SHADER_DIR} NAME)
    set(SPV_FILE ${SHADER_OUT_DIR}/${SHADER_SUBDIR}/${SHADER_NAME}.spv)
    if(${SHADER} MATCHES "\\.vert$")
        set(STAGE_FLAG "-fshader-stage=vert")
    elseif(${SHADER} MATCHES "\\.frag$")
        set(STAGE_FLAG "-fshader-stage=frag")
    elseif(${SHADER} MATCHES "\\.comp$")
        set(STAGE_FLAG "-fshader-stage=comp")
    else()
        continue()
    endif()
    add_custom_command(OUTPUT ${SPV_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}/${SHADER_SUBDIR}
        COMMAND glslc ${STAGE_FLAG} ${SHADER} -o ${SPV_FILE}
        DEPENDS ${SHADER}
        COMMENT "SPIR-V: ${SHADER_SUBDIR}/${SHADER_NAME}")
    list(APPEND ALL_SPV_FILES ${SPV_FILE})
endforeach()
add_custom_target(spv_shaders DEPENDS ${ALL_SPV_FILES})

add_subdirectory(src/core)
add_subdirectory(src/sim)
add_subdirectory(src/render)

qt_add_executable(rtvk
    src/main.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
)
target_link_libraries(rtvk PRIVATE
    rtvk_core rtvk_sim rtvk_render
    Qt6::Core Qt6::Gui Qt6::Widgets
    Vulkan::Vulkan
)
target_include_directories(rtvk PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_dependencies(rtvk spv_shaders)

# Copy SPV files next to executable (must be after qt_add_executable)
add_custom_command(TARGET rtvk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${SHADER_OUT_DIR} $<TARGET_FILE_DIR:rtvk>/shaders
    COMMENT "Copying SPIR-V shaders to output")

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_subdirectory(tests)
    endif()
endif()
