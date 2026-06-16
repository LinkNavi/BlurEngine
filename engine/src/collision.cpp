#include "blur/collision.hpp"

#include <algorithm>
#include <limits>
#include <cmath>

namespace blur {

namespace {

constexpr int kLeafSize = 8;

struct BVHBuilder {
    std::vector<CollisionTriangle>& triangles;
    std::vector<glm::vec3>& centroids;
    std::vector<BVHNode>& nodes;

    int build(std::vector<int>& idx, int start, int count) {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());

        for (int i = start; i < start + count; i++) {
            const auto& t = triangles[idx[i]];
            mn = glm::min(mn, glm::min(t.v0, glm::min(t.v1, t.v2)));
            mx = glm::max(mx, glm::max(t.v0, glm::max(t.v1, t.v2)));
        }

        BVHNode node;
        node.boundsMin = mn;
        node.boundsMax = mx;

        if (count <= kLeafSize) {
            node.triStart = start;
            node.triCount = count;
            nodes.push_back(node);
            return (int)nodes.size() - 1;
        }

        glm::vec3 extent = mx - mn;
        int axis = 0;
        if (extent.y > extent[axis]) axis = 1;
        if (extent.z > extent[axis]) axis = 2;

        int mid = start + count / 2;
        std::nth_element(idx.begin() + start, idx.begin() + mid, idx.begin() + start + count,
            [&](int a, int b) { return centroids[a][axis] < centroids[b][axis]; });

        int leftIdx = build(idx, start, mid - start);
        int rightIdx = build(idx, mid, start + count - mid);

        node.left = leftIdx;
        node.right = rightIdx;
        node.triCount = 0;
        nodes.push_back(node);
        return (int)nodes.size() - 1;
    }
};

bool rayAABB(const glm::vec3& origin, const glm::vec3& invDir,
             const glm::vec3& bmin, const glm::vec3& bmax, float maxDist, float& outT) {
    float t0 = 0.0f, t1 = maxDist;
    for (int a = 0; a < 3; a++) {
        float tNear = (bmin[a] - origin[a]) * invDir[a];
        float tFar  = (bmax[a] - origin[a]) * invDir[a];
        if (tNear > tFar) std::swap(tNear, tFar);
        t0 = std::max(t0, tNear);
        t1 = std::min(t1, tFar);
        if (t0 > t1) return false;
    }
    outT = t0;
    return true;
}

bool rayTriangle(const glm::vec3& origin, const glm::vec3& dir,
                  const CollisionTriangle& tri, float maxDist, float& outT) {
    const float kEps = 1e-7f;
    glm::vec3 e1 = tri.v1 - tri.v0;
    glm::vec3 e2 = tri.v2 - tri.v0;
    glm::vec3 p = glm::cross(dir, e2);
    float det = glm::dot(e1, p);
    if (std::fabs(det) < kEps) return false;
    float invDet = 1.0f / det;

    glm::vec3 tvec = origin - tri.v0;
    float u = glm::dot(tvec, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(tvec, e1);
    float v = glm::dot(dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = glm::dot(e2, q) * invDet;
    if (t < kEps || t > maxDist) return false;

    outT = t;
    return true;
}

glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return a + (d1 / (d1 - d3)) * ab;

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return a + (d2 / (d2 - d6)) * ac;

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + w * (c - b);
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

} // namespace

void CollisionMesh::addModel(const Model& model) {
    for (size_t m = 0; m < model.meshes.size(); m++) {
        const Mesh& mesh = model.meshes[m];
        if (mesh.isSkinned) continue;
        if (m >= model.meshVertices.size() || m >= model.meshIndices.size()) continue;

        const auto& verts = model.meshVertices[m];
        const auto& inds = model.meshIndices[m];

        for (size_t i = 0; i + 2 < inds.size(); i += 3) {
            CollisionTriangle tri;
            tri.v0 = glm::vec3(mesh.transform * glm::vec4(verts[inds[i + 0]].position, 1.0f));
            tri.v1 = glm::vec3(mesh.transform * glm::vec4(verts[inds[i + 1]].position, 1.0f));
            tri.v2 = glm::vec3(mesh.transform * glm::vec4(verts[inds[i + 2]].position, 1.0f));

            glm::vec3 n = glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
            float len = glm::length(n);
            tri.normal = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);

            m_triangles.push_back(tri);
        }
    }
}

void CollisionMesh::finalize() {
    m_nodes.clear();
    m_rootIndex = -1;

    if (m_triangles.empty()) return;

    std::vector<int> idx(m_triangles.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = (int)i;

    std::vector<glm::vec3> centroids(m_triangles.size());
    for (size_t i = 0; i < m_triangles.size(); i++)
        centroids[i] = (m_triangles[i].v0 + m_triangles[i].v1 + m_triangles[i].v2) / 3.0f;

    BVHBuilder builder{ m_triangles, centroids, m_nodes };
    m_rootIndex = builder.build(idx, 0, (int)idx.size());

    std::vector<CollisionTriangle> ordered(m_triangles.size());
    for (size_t i = 0; i < idx.size(); i++)
        ordered[i] = m_triangles[idx[i]];
    m_triangles = std::move(ordered);
}

void CollisionMesh::build(const Model& model) {
    addModel(model);
    finalize();
}

bool CollisionMesh::raycast(const glm::vec3& origin, const glm::vec3& dir, float maxDist, CollisionHit& outHit) const {
    if (m_rootIndex < 0) return false;

    glm::vec3 d = glm::normalize(dir);
    glm::vec3 invDir(
        std::fabs(d.x) > 1e-12f ? 1.0f / d.x : 1e12f,
        std::fabs(d.y) > 1e-12f ? 1.0f / d.y : 1e12f,
        std::fabs(d.z) > 1e-12f ? 1.0f / d.z : 1e12f);

    int stack[64];
    int sp = 0;
    stack[sp++] = m_rootIndex;

    float bestT = maxDist;
    int bestTri = -1;

    while (sp > 0) {
        int nodeIdx = stack[--sp];
        const BVHNode& node = m_nodes[nodeIdx];

        float t;
        if (!rayAABB(origin, invDir, node.boundsMin, node.boundsMax, bestT, t))
            continue;

        if (node.triCount > 0) {
            for (int i = 0; i < node.triCount; i++) {
                float ht;
                if (rayTriangle(origin, d, m_triangles[node.triStart + i], bestT, ht) && ht < bestT) {
                    bestT = ht;
                    bestTri = node.triStart + i;
                }
            }
        } else if (sp < 62) {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }

    if (bestTri < 0) return false;

    outHit.point = origin + d * bestT;
    outHit.normal = m_triangles[bestTri].normal;
    outHit.distance = bestT;
    return true;
}

void CollisionMesh::clear() {
    m_triangles.clear();
    m_nodes.clear();
    m_rootIndex = -1;
}

void CollisionMesh::addTriangles(const std::vector<Vertex>& verts, const std::vector<unsigned int>& indices,
                                  const glm::mat4& transform) {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        CollisionTriangle tri;
        tri.v0 = glm::vec3(transform * glm::vec4(verts[indices[i + 0]].position, 1.0f));
        tri.v1 = glm::vec3(transform * glm::vec4(verts[indices[i + 1]].position, 1.0f));
        tri.v2 = glm::vec3(transform * glm::vec4(verts[indices[i + 2]].position, 1.0f));

        glm::vec3 n = glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
        float len = glm::length(n);
        tri.normal = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);

        m_triangles.push_back(tri);
    }
}

bool CollisionMesh::resolveSphere(glm::vec3& center, float radius, glm::vec3* outNormal) const {
    if (m_rootIndex < 0) return false;

    glm::vec3 qmin = center - glm::vec3(radius);
    glm::vec3 qmax = center + glm::vec3(radius);

    int stack[64];
    int sp = 0;
    stack[sp++] = m_rootIndex;

    bool hit = false;
    glm::vec3 totalPush(0.0f);
    glm::vec3 lastNormal(0, 1, 0);

    while (sp > 0) {
        int nodeIdx = stack[--sp];
        const BVHNode& node = m_nodes[nodeIdx];

        bool overlap = !(node.boundsMax.x < qmin.x || node.boundsMin.x > qmax.x ||
                          node.boundsMax.y < qmin.y || node.boundsMin.y > qmax.y ||
                          node.boundsMax.z < qmin.z || node.boundsMin.z > qmax.z);
        if (!overlap) continue;

        if (node.triCount > 0) {
            for (int i = 0; i < node.triCount; i++) {
                const CollisionTriangle& tri = m_triangles[node.triStart + i];
                glm::vec3 cp = closestPointOnTriangle(center, tri.v0, tri.v1, tri.v2);
                glm::vec3 diff = center - cp;
                float dist = glm::length(diff);
                if (dist < radius && dist > 1e-6f) {
                    totalPush += (diff / dist) * (radius - dist);
                    lastNormal = diff / dist;
                    hit = true;
                }
            }
        } else if (sp < 62) {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }

    if (hit) {
        center += totalPush;
        if (outNormal) *outNormal = glm::normalize(lastNormal);
    }
    return hit;
}

} // namespace blur
