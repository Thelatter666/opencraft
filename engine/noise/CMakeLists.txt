# Noise library (T004): FastNoiseLite wrapper + fBm/ridged sampling.
add_library(opencraft_noise STATIC
    src/noise_sampler.cpp
)

target_include_directories(opencraft_noise PUBLIC include)

# Third-party single header; marked SYSTEM so its warnings do not leak into
# our zero-warning build.
target_include_directories(opencraft_noise SYSTEM PRIVATE ${fastnoise_lite_SOURCE_DIR}/Cpp)

if(MSVC)
    target_compile_options(opencraft_noise PRIVATE /W4)
else()
    target_compile_options(opencraft_noise PRIVATE -Wall -Wextra -Wpedantic)
endif()
