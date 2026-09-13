# Render library (T005): culled mesher pure function + minimal GL 4.1 core
# RHI wrappers + vendored glad loader (see third_party/glad/README.md).
add_library(opencraft_render STATIC
    src/mesher.cpp
    src/rhi.cpp
    third_party/glad/src/gl.c
)

target_include_directories(opencraft_render PUBLIC
    include
    # SYSTEM keeps compiler warnings out of the generated/khronos headers.
    SYSTEM PUBLIC third_party/glad/include
)

target_link_libraries(opencraft_render PUBLIC opencraft_voxel)

if(MSVC)
    target_compile_options(opencraft_render PRIVATE /W4)
else()
    target_compile_options(opencraft_render PRIVATE -Wall -Wextra -Wpedantic)
endif()
