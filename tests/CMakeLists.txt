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
    test_noise.cpp
    test_worldgen.cpp
    test_light_storage.cpp
    test_light_engine.cpp
    test_mesher.cpp
)

# opencraft_worldgen PUBLIC-links opencraft_voxel and opencraft_noise;
# opencraft_render PUBLIC-links opencraft_voxel, so all engine headers
# propagate from here.
target_link_libraries(opencraft_tests PRIVATE opencraft_worldgen opencraft_render doctest::doctest_with_main)

# Golden chunk hashes live next to the tests; the path is baked in so the
# test binary is location-independent.
target_compile_definitions(opencraft_tests
    PRIVATE OPENCRAFT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(opencraft_tests)
