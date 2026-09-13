add_library(opencraft_core STATIC
    src/version.cpp
    src/byte_buffer.cpp
    src/tick_clock.cpp
)

target_include_directories(opencraft_core PUBLIC include)

target_link_libraries(opencraft_core PUBLIC glm::glm spdlog::spdlog)

if(MSVC)
    target_compile_options(opencraft_core PRIVATE /W4)
else()
    target_compile_options(opencraft_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
