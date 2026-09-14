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
    test_player_physics.cpp
    test_sprint_feel.cpp
    test_friction_pipeline.cpp
    test_step_assist.cpp
    test_physics_golden.cpp
    test_raycast.cpp
    test_mining.cpp
    test_placement.cpp
    test_storage.cpp
    test_camera_spring.cpp
)

# opencraft_worldgen PUBLIC-links opencraft_voxel and opencraft_noise;
# opencraft_render PUBLIC-links opencraft_voxel, so all engine headers
# propagate from here. opencraft_physics brings the T007 headless simulation;
# opencraft_game the T008 interaction primitives; opencraft_storage the T009
# persistence layer (PUBLIC zstd headers feed the compression-ratio test).
target_link_libraries(opencraft_tests PRIVATE opencraft_worldgen opencraft_render opencraft_physics opencraft_game
    opencraft_storage doctest::doctest_with_main)

# test_sprint_feel.cpp asserts the sprint FOV easing declared in the
# client's header-only fov.hpp (T-D1), and test_camera_spring.cpp the T-D13
# vertical spring in camera_spring.hpp; the path makes both headers visible.
target_include_directories(opencraft_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../game/client/src)

# Golden chunk hashes live next to the tests; the path is baked in so the
# test binary is location-independent.
target_compile_definitions(opencraft_tests
    PRIVATE OPENCRAFT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(opencraft_tests)
