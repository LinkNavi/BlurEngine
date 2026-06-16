#include "blur/skeleton.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace blur {

glm::vec3 Animation::sampleVec3(const std::vector<float>& times, const std::vector<glm::vec3>& values, float t) {
    if (times.empty()) return glm::vec3(0.0f);
    if (times.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();

    // Find the bracketing keyframes.
    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t i1 = (size_t)(it - times.begin());
    size_t i0 = i1 - 1;

    float t0 = times[i0], t1 = times[i1];
    float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;

    return glm::mix(values[i0], values[i1], alpha);
}

glm::quat Animation::sampleQuat(const std::vector<float>& times, const std::vector<glm::quat>& values, float t) {
    if (times.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (times.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();

    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t i1 = (size_t)(it - times.begin());
    size_t i0 = i1 - 1;

    float t0 = times[i0], t1 = times[i1];
    float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;

    return glm::slerp(values[i0], values[i1], alpha);
}

void Animation::sample(float t, const Skeleton& skeleton, std::vector<glm::mat4>& outLocalTransforms) const {
    outLocalTransforms.resize(skeleton.bones.size());

    // Start every bone at its bind pose; tracks override below.
    for (size_t i = 0; i < skeleton.bones.size(); i++) {
        const Bone& b = skeleton.bones[i];
        glm::mat4 T = glm::translate(glm::mat4(1.0f), b.bindTranslation);
        glm::mat4 R = glm::mat4_cast(b.bindRotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), b.bindScale);
        outLocalTransforms[i] = T * R * S;
    }

    for (const BoneTrack& track : tracks) {
        if (track.boneIndex < 0 || track.boneIndex >= (int)skeleton.bones.size())
            continue;

        const Bone& bind = skeleton.bones[track.boneIndex];

        glm::vec3 translation = track.translationTimes.empty()
            ? bind.bindTranslation
            : sampleVec3(track.translationTimes, track.translations, t);

        glm::quat rotation = track.rotationTimes.empty()
            ? bind.bindRotation
            : sampleQuat(track.rotationTimes, track.rotations, t);

        glm::vec3 scale = track.scaleTimes.empty()
            ? bind.bindScale
            : sampleVec3(track.scaleTimes, track.scales, t);

        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

        outLocalTransforms[track.boneIndex] = T * R * S;
    }
}

void computeSkinningMatrices(
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& localTransforms,
    std::vector<glm::mat4>& outSkinningMatrices) {

    size_t count = skeleton.bones.size();
    std::vector<glm::mat4> worldTransforms(count);

    // Bones are assumed to be stored such that a parent always has a lower
    // index than its children (true for glTF exports in practice, since
    // joints are listed in the skin's joints array which is hierarchy-ordered
    // by every exporter we care about). If that assumption ever breaks for a
    // given asset, this needs a proper topological sort instead.
    for (size_t i = 0; i < count; i++) {
        int parent = skeleton.bones[i].parentIndex;
        if (parent < 0) {
            worldTransforms[i] = localTransforms[i];
        } else {
            worldTransforms[i] = worldTransforms[parent] * localTransforms[i];
        }
    }

    outSkinningMatrices.resize(count);
    for (size_t i = 0; i < count; i++) {
        outSkinningMatrices[i] = worldTransforms[i] * skeleton.bones[i].inverseBindMatrix;
    }
}

} // namespace blur
