#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace blur {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();

    void draw() const;

    glm::mat4 transform = glm::mat4(1.0f);

private:
    unsigned int m_vao, m_vbo, m_ebo;
    unsigned int m_indexCount;
};

// Loads all meshes from a glTF file. Returns false on failure.
bool loadGltf(const std::string& path, std::vector<Mesh>& outMeshes);

} // namespace blur
