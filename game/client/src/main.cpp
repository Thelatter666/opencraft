#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <exception>

#include "gl.hpp"
#include "opencraft/core/log.hpp"
#include "opencraft/core/version.hpp"

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

void error_callback(int error_code, const char *description) {
    OC_LOG_ERROR("GLFW error {}: {}", error_code, description);
}

} // namespace

int main() {
    opencraft::core::log::init();
    OC_LOG_INFO("{} starting", opencraft::core::version_string());

    glfwSetErrorCallback(error_callback);
    if (glfwInit() != GLFW_TRUE) {
        OC_LOG_CRITICAL("glfwInit failed");
        return 1;
    }

    // M0 only needs a context + clear; 4.1 core is the highest portable floor
    // (macOS tops out at 4.1). The render task will move this to 4.3 core.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(kWindowWidth, kWindowHeight, "OpenCraft", nullptr, nullptr);
    if (window == nullptr) {
        OC_LOG_CRITICAL("glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    while (glfwWindowShouldClose(window) != GLFW_TRUE) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Slowly cycling clear color: proof that frames are being presented.
        const double t = glfwGetTime();
        glClearColor(static_cast<float>(0.5 + 0.5 * std::sin(t * 0.5)),
                     static_cast<float>(0.5 + 0.5 * std::sin(t * 0.5 + 2.094)),
                     static_cast<float>(0.5 + 0.5 * std::sin(t * 0.5 + 4.188)), 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    OC_LOG_INFO("clean shutdown");
    return 0;
}
