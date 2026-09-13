# Voxel data layer: chunk storage, section palettes, block registry (T003).
add_library(opencraft_voxel STATIC
    src/block_registry.cpp
    src/chunk.cpp
    src/chunk_manager.cpp
)

target_include_directories(opencraft_voxel PUBLIC include)

target_link_libraries(opencraft_voxel PUBLIC opencraft_core)

if(MSVC)
    target_compile_options(opencraft_voxel PRIVATE /W4)
else()
    target_compile_options(opencraft_voxel PRIVATE -Wall -Wextra -Wpedantic)
endif()
