# doctest is fetched via cmake/deps.cmake; its targets are already available here.

add_executable(opencraft_tests
    test_version.cpp
    test_byte_buffer.cpp
)

target_link_libraries(opencraft_tests PRIVATE opencraft_core doctest::doctest_with_main)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(opencraft_tests)
