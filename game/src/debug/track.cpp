#define GLM_ENABLE_EXPERIMENTAL
#include "track.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <limits>

namespace debugmode {

EndlessTrack::EndlessTrack(float width, float segmentLength, float chunkLength, unsigned int seed)
    : m_width(width), m_segmentLength(segmentLength), m_chunkLength(chunkLength), m_rngState(seed ? seed : 1) {

    TrackFrame start;
    start.position = glm::vec3(0.0f);
    start.tangent = glm::vec3(0, 0, 1);
    start.up = glm::vec3(0, 1, 0);
    start.right = glm::normalize(glm::cross(start.up, start.tangent));
    start.arcLength = 0.0f;
    m_nodes.push_back(start);
}

float EndlessTrack::randomFloat() {
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return (m_rngState & 0xFFFFFFu) / (float)0xFFFFFFu;
}

void EndlessTrack::update(float playerArcLength, float aheadDistance) {
    while (generatedLength() < playerArcLength + aheadDistance) {
        generateNextChunk();
    }
}

void EndlessTrack::generateNextChunk() {
    float chunkStartArc = generatedLength();
    float chunkEndArc = chunkStartArc + m_chunkLength;

    std::vector<blur::Vertex> verts;
    std::vector<unsigned int> indices;

    TrackFrame prev = m_nodes.back();

    auto pushNodeVerts = [&](const TrackFrame& f) {
        blur::Vertex left, rightV;
        left.position = f.position - f.right * (m_width * 0.5f);
        rightV.position = f.position + f.right * (m_width * 0.5f);
        left.normal = f.up;
        rightV.normal = f.up;
        left.uv = glm::vec2(-(m_width * 0.5f) / kTileWorldSize, f.arcLength / kTileWorldSize);
        rightV.uv = glm::vec2((m_width * 0.5f) / kTileWorldSize, f.arcLength / kTileWorldSize);
        verts.push_back(left);
        verts.push_back(rightV);
    };

    pushNodeVerts(prev);

    while (prev.arcLength < chunkEndArc) {
        m_distSinceRetarget += m_segmentLength;
        if (m_distSinceRetarget >= kRetargetInterval) {
            m_yawTarget = (randomFloat() * 2.0f - 1.0f) * kMaxYawCurvature;
            m_pitchTarget = (randomFloat() * 2.0f - 1.0f) * kMaxPitchCurvature;
            m_distSinceRetarget = 0.0f;
        }
        m_yawCurvature += (m_yawTarget - m_yawCurvature) * kCurvatureSmoothing;
        m_pitchCurvature += (m_pitchTarget - m_pitchCurvature) * kCurvatureSmoothing;

        glm::vec3 tangent = prev.tangent;
        glm::vec3 up = prev.up;
        glm::vec3 right = prev.right;

        glm::quat yawRot = glm::angleAxis(m_yawCurvature * m_segmentLength, up);
        tangent = glm::normalize(yawRot * tangent);
        right = glm::normalize(glm::cross(up, tangent));

        glm::quat pitchRot = glm::angleAxis(m_pitchCurvature * m_segmentLength, right);
        tangent = glm::normalize(pitchRot * tangent);
        up = glm::normalize(pitchRot * up);
        right = glm::normalize(glm::cross(up, tangent));
        up = glm::normalize(glm::cross(tangent, right));

        TrackFrame next;
        next.position = prev.position + tangent * m_segmentLength;
        next.tangent = tangent;
        next.up = up;
        next.right = right;
        next.arcLength = prev.arcLength + m_segmentLength;

        m_nodes.push_back(next);
        pushNodeVerts(next);

        unsigned int base = (unsigned int)verts.size() - 4;
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);

        prev = next;
    }

    Chunk chunk{ blur::Mesh(verts, indices), chunkStartArc, prev.arcLength };
    m_chunks.push_back(std::move(chunk));
}

void EndlessTrack::trim(float playerArcLength, float behindDistance) {
    float cutoff = playerArcLength - behindDistance;
    while (!m_chunks.empty() && m_chunks.front().endArc < cutoff)
        m_chunks.pop_front();
    while (m_nodes.size() > 1 && m_nodes.front().arcLength < cutoff)
        m_nodes.pop_front();
}

TrackFrame EndlessTrack::sampleFrame(float s) const {
    if (m_nodes.empty()) {
        TrackFrame f;
        f.position = glm::vec3(0.0f);
        f.tangent = glm::vec3(0, 0, 1);
        f.up = glm::vec3(0, 1, 0);
        f.right = glm::vec3(1, 0, 0);
        f.arcLength = 0.0f;
        return f;
    }

    s = glm::clamp(s, m_nodes.front().arcLength, m_nodes.back().arcLength);

    size_t lo = 0, hi = m_nodes.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (m_nodes[mid].arcLength <= s) lo = mid; else hi = mid;
    }

    const TrackFrame& a = m_nodes[lo];
    const TrackFrame& b = m_nodes[hi];
    float span = b.arcLength - a.arcLength;
    float t = span > 1e-6f ? (s - a.arcLength) / span : 0.0f;

    TrackFrame out;
    out.position = glm::mix(a.position, b.position, t);
    out.tangent = glm::normalize(glm::mix(a.tangent, b.tangent, t));
    out.up = glm::normalize(glm::mix(a.up, b.up, t));
    out.right = glm::normalize(glm::cross(out.up, out.tangent));
    out.up = glm::normalize(glm::cross(out.tangent, out.right));
    out.arcLength = s;
    return out;
}

float EndlessTrack::closestArcLength(const glm::vec3& worldPos, float hintArc, float searchRadius) const {
    if (m_nodes.empty()) return 0.0f;

    float lo = glm::clamp(hintArc - searchRadius, m_nodes.front().arcLength, m_nodes.back().arcLength);
    float hi = glm::clamp(hintArc + searchRadius, m_nodes.front().arcLength, m_nodes.back().arcLength);

    // Coarse scan over the windowed node range to find the nearest node,
    // then refine with a couple of golden-section-ish bisection steps
    // against sampleFrame so we're not stuck at segment resolution.
    float bestArc = glm::clamp(hintArc, m_nodes.front().arcLength, m_nodes.back().arcLength);
    float bestDist = std::numeric_limits<float>::max();

    for (const TrackFrame& n : m_nodes) {
        if (n.arcLength < lo || n.arcLength > hi) continue;
        float d = glm::length2(n.position - worldPos);
        if (d < bestDist) {
            bestDist = d;
            bestArc = n.arcLength;
        }
    }

    float step = m_segmentLength;
    for (int i = 0; i < 6; i++) {
        step *= 0.5f;
        float left = bestArc - step;
        float right = bestArc + step;

        float dl = glm::length2(sampleFrame(left).position - worldPos);
        float dr = glm::length2(sampleFrame(right).position - worldPos);
        float dc = glm::length2(sampleFrame(bestArc).position - worldPos);

        if (dl < dc && dl <= dr) bestArc = left;
        else if (dr < dc && dr <= dl) bestArc = right;
    }

    return glm::clamp(bestArc, m_nodes.front().arcLength, m_nodes.back().arcLength);
}

void EndlessTrack::draw() const {
    for (const auto& chunk : m_chunks)
        chunk.mesh.draw();
}

} // namespace debugmode
