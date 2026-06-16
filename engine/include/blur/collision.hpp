#pragma once

#include "blur/mesh.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace blur {

struct CollisionTriangle {
    glm::vec3 v0, v1, v2;
    glm::vec3 normal;
};

struct CollisionHit {
    glm::vec3 point;
    glm::vec3 normal;
    float distance = 0.0f;
};

struct BVHNode {
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    int left = -1;
    int right = -1;
    int triStart = 0;
    int triCount = 0;
};

class CollisionMesh {
public:
    // Appends triangles from a model's non-skinned (static/world) meshes.
    // Call finalize() once after all addModel() calls before querying.
    void addModel(const Model& model);

    // Builds the BVH from everything added so far.
    void finalize();

    // Convenience for the single-model case: addModel + finalize.
    void build(const Model& model);

    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, CollisionHit& outHit) const;
    bool resolveSphere(glm::vec3& center, float radius, glm::vec3* outNormal = nullptr) const;
    void addTriangles(const std::vector<Vertex>& verts, const std::vector<unsigned int>& indices,
                       const glm::mat4& transform = glm::mat4(1.0f));
    void clear();
    bool empty() const { return m_triangles.empty(); }

private:
    std::vector<CollisionTriangle> m_triangles;
    std::vector<BVHNode> m_nodes;
    int m_rootIndex = -1;
};

} // namespace blur
