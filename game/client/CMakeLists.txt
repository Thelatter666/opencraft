find_package(OpenGL REQUIRED)

add_executable(opencraft
    src/main.cpp
)

target_include_directories(opencraft PRIVATE src)

# Contract: the client is invoked as ./build/opencraft from the repo root.
set_target_properties(opencraft PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_libraries(opencraft PRIVATE
    opencraft_core
    opencraft_common
    OpenGL::GL
    glfw
)
