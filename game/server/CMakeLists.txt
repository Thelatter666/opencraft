# Server-side world generation (T004): pure-function terrain generator.
add_library(opencraft_worldgen STATIC
    worldgen/src/terrain_generator.cpp
)

target_include_directories(opencraft_worldgen PUBLIC worldgen/include)

target_link_libraries(opencraft_worldgen
    PUBLIC
        opencraft_voxel
        opencraft_noise
)

if(MSVC)
    target_compile_options(opencraft_worldgen PRIVATE /W4)
else()
    target_compile_options(opencraft_worldgen PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Persistence (T009): region files (zstd) + level file + save orchestration.
add_library(opencraft_storage STATIC
    storage/src/region_file.cpp
    storage/src/level_file.cpp
    storage/src/world_save.cpp
)

target_include_directories(opencraft_storage PUBLIC storage/include)

target_link_libraries(opencraft_storage
    PUBLIC
        opencraft_core
        opencraft_voxel
        # PUBLIC so the test suite can measure zstd ratios directly (T009).
        libzstd_static
)

if(MSVC)
    target_compile_options(opencraft_storage PRIVATE /W4)
else()
    target_compile_options(opencraft_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The authoritative side (T-A1): the world simulation that owns the world's
# mutable state and is the only place a block or a fluid cell is written. The
# client links it and drives it in-process (docs/03 §1: 客户端+内嵌服务端);
# M3 puts a network channel in front of the same three verbs.
add_library(opencraft_sim STATIC
    sim/src/world_sim.cpp
)

target_include_directories(opencraft_sim PUBLIC sim/include)

target_link_libraries(opencraft_sim
    PUBLIC
        opencraft_game
        opencraft_worldgen
        opencraft_storage
        opencraft_physics
)

if(MSVC)
    target_compile_options(opencraft_sim PRIVATE /W4)
else()
    target_compile_options(opencraft_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The server executable arrives in a later task; the authoritative simulation
# is its first real component, so keep the interface target wired to it.
add_library(opencraft_server INTERFACE)
target_link_libraries(opencraft_server INTERFACE opencraft_sim)
