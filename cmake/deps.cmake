# Third-party dependencies, fetched at configure time (docs/03 §9).
# GLFW is hard-required by T001 (window creation is an acceptance criterion);
# glm / spdlog / doctest are wired in now for later tasks and may be unused.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.1
)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
)

FetchContent_MakeAvailable(glfw glm spdlog doctest)

# FastNoiseLite (T004): single-header portable noise library, MIT license.
# Source: https://github.com/Auburn/FastNoiseLite (tag v1.1.1); the C++ header
# lives at Cpp/FastNoiseLite.h. The repo has no CMakeLists, so MakeAvailable
# only populates the source directory; engine/noise consumes the header.
FetchContent_Declare(
    fastnoise_lite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG v1.1.1
)

FetchContent_MakeAvailable(fastnoise_lite)

# zstd (T009): region-file chunk compression (docs/03 §7, research/03 §5.2).
# Static lib only; we consume the C API. SOURCE_SUBDIR points at the bundled
# CMake project (the repo root is plain Makefiles).
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(zstd)
