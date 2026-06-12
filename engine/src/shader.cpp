#include "blur/shader.hpp"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstdlib>

namespace blur {

static unsigned int compile(GLenum type, const char* src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
        std::exit(1);
    }
    return shader;
}

Shader::Shader(const char* vertSrc, const char* fragSrc) {
    unsigned int vert = compile(GL_VERTEX_SHADER, vertSrc);
    unsigned int frag = compile(GL_FRAGMENT_SHADER, fragSrc);

    m_id = glCreateProgram();
    glAttachShader(m_id, vert);
    glAttachShader(m_id, frag);
    glLinkProgram(m_id);

    int success;
    glGetProgramiv(m_id, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(m_id, 512, nullptr, log);
        std::fprintf(stderr, "Shader link error: %s\n", log);
        std::exit(1);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() {
    glDeleteProgram(m_id);
}

void Shader::use() const {
    glUseProgram(m_id);
}

void Shader::setMat4(const char* name, const glm::mat4& mat) const {
    int loc = glGetUniformLocation(m_id, name);
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::setVec3(const char* name, const glm::vec3& v) const {
    int loc = glGetUniformLocation(m_id, name);
    glUniform3fv(loc, 1, glm::value_ptr(v));
}

void Shader::setInt(const char* name, int v) const {
    int loc = glGetUniformLocation(m_id, name);
    glUniform1i(loc, v);
}

} // namespace blur
