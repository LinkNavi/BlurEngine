#pragma once

#include <glm/glm.hpp>
#include <string>

namespace blur {

class Shader {
public:
    Shader(const char* vertSrc, const char* fragSrc);
    ~Shader();

    void use() const;
    void setMat4(const char* name, const glm::mat4& mat) const;
    void setVec3(const char* name, const glm::vec3& v) const;
    void setInt(const char* name, int v) const;

    unsigned int id() const { return m_id; }

private:
    unsigned int m_id;
};

} // namespace blur
