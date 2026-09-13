# Shared game logic (T008): interaction primitives used by the client (and,
# from M2 on, the server) in namespace opencraft::game - DDA voxel raycast,
# mining progress state machine, placement rules. Links the engine layers the
# task contract names (voxel/render/physics/core).
add_library(opencraft_game STATIC
    src/raycast.cpp
    src/mining.cpp
    src/placement.cpp
)

target_include_directories(opencraft_game PUBLIC include)

target_link_libraries(opencraft_game
    PUBLIC
        opencraft_voxel
        opencraft_render
        opencraft_physics
        opencraft_core
)

if(MSVC)
    target_compile_options(opencraft_game PRIVATE /W4)
else()
    target_compile_options(opencraft_game PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The T001 interface target stays so existing consumers keep linking.
add_library(opencraft_common INTERFACE)
