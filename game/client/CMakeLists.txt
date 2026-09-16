find_package(OpenGL REQUIRED)

add_executable(opencraft
    src/main.cpp
    src/world.cpp
    src/atlas.cpp
    src/shaders.cpp
    src/bitmap_font.cpp
    src/cube_geometry.cpp
    src/particles.cpp
    src/block_colors.cpp
    src/chunk_renderer.cpp
    src/hud.cpp
    src/pause_menu.cpp
    src/tick.cpp
)

target_include_directories(opencraft PRIVATE src)

# Contract: the client is invoked as ./build/opencraft from the repo root.
set_target_properties(opencraft PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# opencraft_game carries the T008 interaction primitives; opencraft_worldgen
# the T004 terrain generator the client now streams with (both PUBLIC-link
# the voxel layer). opencraft_storage is the T009 persistence layer.
# opencraft_sim is the T-A1 authoritative side: the client owns it and drives
# it in-process, but reaches its world only through the read-only view.
target_link_libraries(opencraft PRIVATE
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
