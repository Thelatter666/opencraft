# Player physics library (T007): headless tick-level simulation — per-axis
# AABB + substep voxel collision, spec-exact movement numbers (docs/01 §2).
add_library(opencraft_physics STATIC
    src/player_physics.cpp
)

target_include_directories(opencraft_physics PUBLIC include)

# IBlockSource keeps the dependency on the voxel layer directional (the
# runtime ChunkManager + BlockRegistry adapter lives with the game layer,
# T008+); opencraft_voxel is linked per the task card so the adapter can be
# added without touching this file.
target_link_libraries(opencraft_physics PUBLIC opencraft_voxel opencraft_core)

if(MSVC)
    target_compile_options(opencraft_physics PRIVATE /W4)
else()
    target_compile_options(opencraft_physics PRIVATE -Wall -Wextra -Wpedantic)
endif()
