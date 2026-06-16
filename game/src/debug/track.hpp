#pragma once

#include "blur/mesh.hpp"
#include "blur/collision.hpp"
#include <glm/glm.hpp>
#include <deque>
#include <vector>

namespace debugmode {

struct TrackFrame {
    glm::vec3 position;
    glm::vec3 tangent;
    glm::vec3 up;
    glm::vec3 right;
    float arcLength;
};

// Procedural endless test track: smooth random bends left/right (yaw) and
// up/down (pitch), generated and rendered in fixed-length chunks so memory
// stays bounded as the player advances. Debug/test scaffolding only.
class EndlessTrack {
public:
    EndlessTrack(float width, float segmentLength, float chunkLength, unsigned int seed);
const blur::CollisionMesh& collision() const;
    void update(float playerArcLength, float aheadDistance);
    void trim(float playerArcLength, float behindDistance);

    TrackFrame sampleFrame(float s) const;

    // Finds the arc length of the node closest to worldPos, searching
    // outward from hintArc. Since the player moves continuously frame to
    // frame, last frame's arc length is a near-perfect hint, so this stays
    // O(1)-ish instead of scanning the whole node deque.
    float closestArcLength(const glm::vec3& worldPos, float hintArc, float searchRadius = 12.0f) const;

    float generatedLength() const { return m_nodes.empty() ? 0.0f : m_nodes.back().arcLength; }
    void draw() const;
    float width() const { return m_width; }

private:
    struct Chunk {
        blur::Mesh mesh;
        std::vector<blur::Vertex> verts;
        std::vector<unsigned int> indices;
        float startArc;
        float endArc;
    };

    float m_yawHeading   = 0.0f; // accumulated yaw angle from start direction
    float m_pitchHeading = 0.0f; // accumulated pitch angle (tilt up/down)
    void generateNextChunk();
    float randomFloat();
    mutable blur::CollisionMesh m_collision;
    mutable bool m_collisionDirty = false;
    void rebuildCollision() const;
    float m_width;
    float m_segmentLength;
    float m_chunkLength;
    unsigned int m_rngState;

    std::deque<TrackFrame> m_nodes;
    std::deque<Chunk> m_chunks;

    float m_yawCurvature = 0.0f, m_yawTarget = 0.0f;
    float m_pitchCurvature = 0.0f, m_pitchTarget = 0.0f;
    float m_distSinceRetarget = 0.0f;

    static constexpr float kMaxYawCurvature = 0.35f;
    static constexpr float kMaxPitchCurvature = 0.12f;
    static constexpr float kRetargetInterval = 14.0f;
    static constexpr float kCurvatureSmoothing = 0.04f;
    static constexpr float kMaxYawHeading   = glm::radians(100.0f); // hard cap, well short of 180 — track can never curve back into a loop
    static constexpr float kMaxPitchHeading = glm::radians(35.0f);  // how steep it can tilt up/down
    static constexpr float kTileWorldSize = 2.0f;
};

} // namespace debugmode
