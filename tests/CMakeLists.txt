# doctest is fetched via cmake/deps.cmake; its targets are already available here.

add_executable(opencraft_tests
    test_version.cpp
    test_byte_buffer.cpp
    test_serialization.cpp
    test_tick_clock.cpp
    test_math.cpp
    test_task_system.cpp
    test_log.cpp
    test_block_registry.cpp
    test_chunk.cpp
    test_chunk_manager.cpp
    test_light_storage.cpp
    test_light_engine.cpp
)

# opencraft_voxel PUBLIC-links opencraft_core, so headers propagate from here.
target_link_libraries(opencraft_tests PRIVATE opencraft_voxel doctest::doctest_with_main)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(opencraft_tests)
