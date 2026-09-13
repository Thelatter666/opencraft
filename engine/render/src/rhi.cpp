#include "opencraft/render/rhi.hpp"

#include <stdexcept>
#include <utility>

namespace opencraft::render {

namespace {

// macOS caps out at GL 4.1 core, so everything here sticks to the classic
// binding-based entry points (no DSA); the loader is glad, loaded in main().

void throw_on_shader_failure(unsigned int handle, bool is_program, std::string_view what) {
    int ok = 0;
    if (is_program) {
        glGetProgramiv(handle, GL_LINK_STATUS, &ok);
    } else {
        glGetShaderiv(handle, GL_COMPILE_STATUS, &ok);
    }
    if (ok != GL_FALSE) {
        return;
    }
    int log_length = 0;
    if (is_program) {
        glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    } else {
        glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
    }
    std::vector<char> log(static_cast<std::size_t>(log_length) + 1, '\0');
    if (is_program) {
        glGetProgramInfoLog(handle, log_length, nullptr, log.data());
    } else {
        glGetShaderInfoLog(handle, log_length, nullptr, log.data());
    }
    throw std::runtime_error(std::string(what) + ": " + log.data());
}

} // namespace

Shader::Shader(std::string_view vertex_source, std::string_view fragment_source) {
    const unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    const unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (vs == 0 || fs == 0) {
        throw std::runtime_error("Shader: glCreateShader failed (no GL context current?)");
    }
    const char *vs_ptr = vertex_source.data();
    const char *fs_ptr = fragment_source.data();
    const int vs_len = static_cast<int>(vertex_source.size());
    const int fs_len = static_cast<int>(fragment_source.size());
    glShaderSource(vs, 1, &vs_ptr, &vs_len);
    glShaderSource(fs, 1, &fs_ptr, &fs_len);
    glCompileShader(vs);
    glCompileShader(fs);
    throw_on_shader_failure(vs, false, "vertex shader compile failed");
    throw_on_shader_failure(fs, false, "fragment shader compile failed");

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    throw_on_shader_failure(program_, true, "program link failed");
}

Shader::~Shader() {
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
}

Shader::Shader(Shader &&other) noexcept : program_(std::exchange(other.program_, 0)) {
}

Shader &Shader::operator=(Shader &&other) noexcept {
    if (this != &other) {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

void Shader::use() const {
    glUseProgram(program_);
}

int Shader::uniform_location(std::string_view name) const {
    const std::string null_terminated(name);
    return glGetUniformLocation(program_, null_terminated.c_str());
}

Buffer::Buffer(Target target, const void *data, std::size_t bytes, Usage usage) {
    target_ = target == Target::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    glGenBuffers(1, &id_);
    glBindBuffer(target_, id_);
    const GLenum gl_usage = usage == Usage::Dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBufferData(target_, static_cast<GLsizeiptr>(bytes), data, gl_usage);
}

Buffer::~Buffer() {
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
    }
}

Buffer::Buffer(Buffer &&other) noexcept : id_(std::exchange(other.id_, 0)), target_(other.target_) {
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteBuffers(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
        target_ = other.target_;
    }
    return *this;
}

void Buffer::bind() const {
    glBindBuffer(target_, id_);
}

unsigned int Buffer::id() const {
    return id_;
}

VertexArray::VertexArray() {
    glGenVertexArrays(1, &id_);
}

VertexArray::~VertexArray() {
    if (id_ != 0) {
        glDeleteVertexArrays(1, &id_);
    }
}

VertexArray::VertexArray(VertexArray &&other) noexcept : id_(std::exchange(other.id_, 0)) {
}

VertexArray &VertexArray::operator=(VertexArray &&other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteVertexArrays(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void VertexArray::bind() const {
    glBindVertexArray(id_);
}

void VertexArray::set_attribute(int location, int components, unsigned int type, std::size_t stride,
                                std::size_t offset) {
    // Requires: this VAO and the source Buffer are currently bound (classic
    // binding-based setup; state is captured into the VAO).
    glEnableVertexAttribArray(static_cast<unsigned int>(location));
    glVertexAttribPointer(static_cast<unsigned int>(location), components, type, GL_FALSE, static_cast<GLsizei>(stride),
                          reinterpret_cast<const void *>(offset));
}

Texture2D::Texture2D(int width, int height, const std::uint32_t *pixels) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

Texture2D::~Texture2D() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
    }
}

Texture2D::Texture2D(Texture2D &&other) noexcept : id_(std::exchange(other.id_, 0)) {
}

Texture2D &Texture2D::operator=(Texture2D &&other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteTextures(1, &id_);
        }
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

void Texture2D::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(unit));
    glBindTexture(GL_TEXTURE_2D, id_);
}

} // namespace opencraft::render
