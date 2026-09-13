#pragma once

// Platform GL header shim for M0. Only GL 1.0 entry points (glViewport /
// glClearColor / glClear) are used; those are exported directly by the system
// GL library on all three platforms, so no loader (glad/gl3w) is needed yet.
// The render task will introduce a proper loader for GL 4.3 core (docs/03 §4).

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GL/gl.h>
#include <windows.h>
#elif defined(__APPLE__)
// System OpenGL is deprecated on macOS; that is a known platform fact, not a
// code defect.
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
