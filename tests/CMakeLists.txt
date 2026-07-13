# ── RTVK Unit Tests (GoogleTest) ──────────────

add_executable(test_rtvk
    test_pbd.cpp
    test_spatial_hash.cpp
    test_vulkan.cpp
)

target_link_libraries(test_rtvk PRIVATE
    rtvk_core
    rtvk_sim
    rtvk_render
    GTest::GTest
    GTest::Main
)

target_include_directories(test_rtvk PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(test_rtvk)
