find_package(OpenGL REQUIRED)

# T-A2: the atlas generator and the external art-asset loader live in their own
# static library. Neither file touches GL or GLFW, so tests/test_asset_atlas.cpp
# links the exact code the client runs instead of a second copy of it.
add_library(opencraft_client_assets STATIC
    src/atlas.cpp
    src/asset_atlas.cpp
)

target_include_directories(opencraft_client_assets PUBLIC src)

# stb_image: single-header public-domain PNG decoder, consumed only by
# asset_atlas.cpp. SYSTEM keeps its warnings out of our zero-warning build, the
# same treatment FastNoiseLite gets in engine/noise.
target_include_directories(opencraft_client_assets SYSTEM PRIVATE ${stb_SOURCE_DIR})

target_link_libraries(opencraft_client_assets PUBLIC opencraft_render opencraft_core)

if(MSVC)
    target_compile_options(opencraft_client_assets PRIVATE /W4)
else()
    target_compile_options(opencraft_client_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

# T-B1: the mob model channel's pure half - the .vox decoder, the culled
# mesher and the pose math. None of the three touches GL, so
# tests/test_mob_model.cpp asserts on the same code the client runs rather than
# on a copy of it. The GL half (mob_render.cpp) stays in the executable, where
# a context is guaranteed to exist.
add_library(opencraft_client_mobs STATIC
    src/mob_model.cpp
    src/mob_mesh.cpp
    src/mob_pose.cpp
)

target_include_directories(opencraft_client_mobs PUBLIC src)

# The palette PNG goes through the asset channel's 16x16 loader (T-A2), which
# is also where the "one WARN for a broken file, silence for a missing one"
# rule lives.
target_link_libraries(opencraft_client_mobs PUBLIC opencraft_client_assets opencraft_core)

if(MSVC)
    target_compile_options(opencraft_client_mobs PRIVATE /W4)
else()
    target_compile_options(opencraft_client_mobs PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(opencraft
    src/main.cpp
    src/world.cpp
    src/shaders.cpp
    src/bitmap_font.cpp
    src/cube_geometry.cpp
    src/particles.cpp
    src/block_colors.cpp
    src/chunk_renderer.cpp
    src/hud.cpp
    src/pause_menu.cpp
    src/tick.cpp
    src/mob_render.cpp
)

target_include_directories(opencraft PRIVATE src)

# Contract: the client is invoked as ./build/opencraft from the repo root.
set_target_properties(opencraft PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# opencraft_game carries the T008 interaction primitives; opencraft_worldgen
# the T004 terrain generator the client now streams with (both PUBLIC-link
# the voxel layer). opencraft_storage is the T009 persistence layer.
# opencraft_sim is the T-A1 authoritative side: the client owns it and drives
# it in-process, but reaches its world only through the read-only view.
# opencraft_client_assets owns atlas.cpp / asset_atlas.cpp (T-A2);
# opencraft_client_mobs owns the T-B1 .vox decoder + mesher + pose math. The
# atlas loader is not listed twice on purpose: opencraft_client_mobs links it
# PUBLIC (the palette PNG goes through it), so it arrives here transitively and
# the link line stays free of a duplicate library.
target_link_libraries(opencraft PRIVATE
    opencraft_client_mobs
    opencraft_game
    opencraft_worldgen
    opencraft_render
    opencraft_physics
    opencraft_storage
    opencraft_sim
    OpenGL::GL
    glfw
)

if(MSVC)
    target_compile_options(opencraft PRIVATE /W4)
else()
    target_compile_options(opencraft PRIVATE -Wall -Wextra -Wpedantic)
endif()
