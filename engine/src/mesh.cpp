#include "blur/mesh.hpp"

#include <glad/glad.h>

#define CGLTF_IMPLEMENTATION
#include "blur/external/cgltf.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>

namespace blur {

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    m_indexCount = (unsigned int)indices.size();

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));

    glBindVertexArray(0);
}

Mesh::~Mesh() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}

void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

static void loadNode(cgltf_node* node, const glm::mat4& parentTransform, std::vector<Mesh>& outMeshes);

static glm::mat4 nodeLocalTransform(cgltf_node* node) {
    glm::mat4 m(1.0f);
    if (node->has_matrix) {
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++)
                m[c][r] = node->matrix[c * 4 + r];
        return m;
    }

    glm::vec3 t(0.0f), s(1.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);

    if (node->has_translation)
        t = glm::vec3(node->translation[0], node->translation[1], node->translation[2]);
    if (node->has_rotation)
        r = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
    if (node->has_scale)
        s = glm::vec3(node->scale[0], node->scale[1], node->scale[2]);

    m = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
    return m;
}

static void loadMesh(cgltf_mesh* mesh, const glm::mat4& transform, std::vector<Mesh>& outMeshes) {
    for (size_t p = 0; p < mesh->primitives_count; p++) {
        cgltf_primitive* prim = &mesh->primitives[p];
        if (prim->type != cgltf_primitive_type_triangles)
            continue;

        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        cgltf_accessor* posAcc = nullptr;
        cgltf_accessor* normAcc = nullptr;
        cgltf_accessor* uvAcc = nullptr;

        for (size_t a = 0; a < prim->attributes_count; a++) {
            cgltf_attribute* attr = &prim->attributes[a];
            if (attr->type == cgltf_attribute_type_position) posAcc = attr->data;
            else if (attr->type == cgltf_attribute_type_normal) normAcc = attr->data;
            else if (attr->type == cgltf_attribute_type_texcoord && attr->index == 0) uvAcc = attr->data;
        }

        if (!posAcc) continue;

        size_t vertCount = posAcc->count;
        vertices.resize(vertCount);

        for (size_t i = 0; i < vertCount; i++) {
            cgltf_accessor_read_float(posAcc, i, &vertices[i].position.x, 3);

            if (normAcc)
                cgltf_accessor_read_float(normAcc, i, &vertices[i].normal.x, 3);
            else
                vertices[i].normal = glm::vec3(0, 1, 0);

            if (uvAcc)
                cgltf_accessor_read_float(uvAcc, i, &vertices[i].uv.x, 2);
            else
                vertices[i].uv = glm::vec2(0.0f);
        }

        if (prim->indices) {
            indices.resize(prim->indices->count);
            for (size_t i = 0; i < prim->indices->count; i++)
                indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
        } else {
            indices.resize(vertCount);
            for (size_t i = 0; i < vertCount; i++)
                indices[i] = (unsigned int)i;
        }

        Mesh m(vertices, indices);
        m.transform = transform;
        outMeshes.push_back(std::move(m));
    }
}

static void loadNode(cgltf_node* node, const glm::mat4& parentTransform, std::vector<Mesh>& outMeshes) {
    glm::mat4 worldTransform = parentTransform * nodeLocalTransform(node);

    if (node->mesh)
        loadMesh(node->mesh, worldTransform, outMeshes);

    for (size_t i = 0; i < node->children_count; i++)
        loadNode(node->children[i], worldTransform, outMeshes);
}

bool loadGltf(const std::string& path, std::vector<Mesh>& outMeshes) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        std::fprintf(stderr, "Failed to parse glTF: %s\n", path.c_str());
        return false;
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        std::fprintf(stderr, "Failed to load buffers for glTF: %s\n", path.c_str());
        cgltf_free(data);
        return false;
    }

    for (size_t s = 0; s < data->scenes_count; s++) {
        cgltf_scene* scene = &data->scenes[s];
        for (size_t n = 0; n < scene->nodes_count; n++)
            loadNode(scene->nodes[n], glm::mat4(1.0f), outMeshes);
    }

    cgltf_free(data);
    return true;
}

} // namespace blur
