#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <glad/gl.h>

namespace opencraft::render {

// Minimal RAII wrappers over the GL 4.1 core subset the engine currently
// needs (task T005: Shader/VBO/VAO/Texture are enough; docs/03 §4 calls for a
// fuller RHI abstraction in a later milestone). All classes are non-copyable;
// owning wrappers release their GL name on destruction.

// GLSL program compiled from in-memory sources; throws std::runtime_error
// with the info log on failure.
class Shader {
public:
    Shader(std::string_view vertex_source, std::string_view fragment_source);
    ~Shader();
    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    void use() const;
    [[nodiscard]] int uniform_location(std::string_view name) const;

private:
    unsigned int program_ = 0;
};

// Vertex or index buffer.
class Buffer {
public:
    enum class Target { Vertex, Index };
    enum class Usage { Static, Dynamic };

    Buffer(Target target, const void *data, std::size_t bytes, Usage usage);
    ~Buffer();
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;

    void bind() const;

    [[nodiscard]] unsigned int id() const;

private:
    unsigned int id_ = 0;
    unsigned int target_ = GL_ARRAY_BUFFER;
};

// Vertex array object. Attribute setup is binding-based (GL 4.1 core has no
// DSA): bind() the VAO, bind() the vertex Buffer, then configure attributes;
// bind an index Buffer while the VAO is bound to attach it.
class VertexArray {
public:
    VertexArray();
    ~VertexArray();
    VertexArray(const VertexArray &) = delete;
    VertexArray &operator=(const VertexArray &) = delete;
    VertexArray(VertexArray &&other) noexcept;
    VertexArray &operator=(VertexArray &&other) noexcept;

    void bind() const;

    // Describes one float-converted attribute (non-normalized integer data
    // arrives in the shader as float, which is all the packed mesh format
    // needs). Requires: this VAO and the source vertex Buffer are bound.
    void set_attribute(int location, int components, unsigned int type, std::size_t stride, std::size_t offset);

private:
    unsigned int id_ = 0;
};

// 2D RGBA8 texture (GL_NEAREST, clamp to edge) used for the placeholder
// atlas; `pixels` is width*height non-premultiplied RGBA, row 0 first.
class Texture2D {
public:
    Texture2D(int width, int height, const std::uint32_t *pixels);
    ~Texture2D();
    Texture2D(const Texture2D &) = delete;
    Texture2D &operator=(const Texture2D &) = delete;
    Texture2D(Texture2D &&other) noexcept;
    Texture2D &operator=(Texture2D &&other) noexcept;

    void bind(int unit) const;

private:
    unsigned int id_ = 0;
};

} // namespace opencraft::render
