#pragma once

#include "blur/mesh.hpp"
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

    void update(float playerArcLength, float aheadDistance);
    void trim(float playerArcLength, float behindDistance);

    TrackFrame sampleFrame(float s) const;

    float generatedLength() const { return m_nodes.empty() ? 0.0f : m_nodes.back().arcLength; }
    void draw() const;
    float width() const { return m_width; }

private:
    struct Chunk {
        blur::Mesh mesh;
        float startArc;
        float endArc;
    };

    void generateNextChunk();
    float randomFloat();

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
    static constexpr float kTileWorldSize = 2.0f;
};

} // namespace debugmode
