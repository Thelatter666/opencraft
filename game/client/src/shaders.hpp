#pragma once

// GLSL sources for the client renderer. Moved verbatim out of main.cpp by
// T-M1 (pure code motion): the raw-string bodies are byte-identical to the
// pre-split file, and only the storage class changed (constexpr -> extern
// const), so every translation unit shares one copy.

namespace opencraft::client {

extern const char kVertexShader[];
extern const char kFragmentShader[];
extern const char kWireVertexShader[];
extern const char kWireFragmentShader[];
extern const char kCrackVertexShader[];
extern const char kCrackFragmentShader[];
extern const char kParticleVertexShader[];
extern const char kParticleFragmentShader[];
extern const char kUiFlatVertexShader[];
extern const char kUiFlatFragmentShader[];
extern const char kUiTextVertexShader[];
extern const char kUiTextFragmentShader[];

} // namespace opencraft::client
