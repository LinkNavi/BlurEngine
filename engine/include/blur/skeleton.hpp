#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace blur {

// A single joint in the skeleton hierarchy.
struct Bone {
    std::string name;
    int parentIndex = -1;          // index into Skeleton::bones, -1 = root
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);

    // Local bind-pose transform (used as the rest pose if no animation is playing).
    glm::vec3 bindTranslation = glm::vec3(0.0f);
    glm::quat bindRotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 bindScale       = glm::vec3(1.0f);
};

struct Skeleton {
    std::vector<Bone> bones;

    int findBone(const std::string& name) const {
        for (size_t i = 0; i < bones.size(); i++)
            if (bones[i].name == name) return (int)i;
        return -1;
    }
};

// Per-bone animated keyframe tracks. Times are in seconds, matched to glTF's
// own accessor sample times (not necessarily uniform spacing).
struct BoneTrack {
    int boneIndex = -1;

    std::vector<float> translationTimes;
    std::vector<glm::vec3> translations;

    std::vector<float> rotationTimes;
    std::vector<glm::quat> rotations;

    std::vector<float> scaleTimes;
    std::vector<glm::vec3> scales;
};

class Animation {
public:
    std::string name;
    float duration = 0.0f; // seconds
    std::vector<BoneTrack> tracks;

    // Samples this animation at time t (seconds, will wrap/clamp by caller)
    // into outLocalTransforms, one entry per bone in the skeleton (by index).
    // Bones with no track keep their bind pose (skeleton must be passed so we
    // know the size + bind fallback).
    void sample(float t, const Skeleton& skeleton, std::vector<glm::mat4>& outLocalTransforms) const;

private:
    static glm::vec3 sampleVec3(const std::vector<float>& times, const std::vector<glm::vec3>& values, float t);
    static glm::quat sampleQuat(const std::vector<float>& times, const std::vector<glm::quat>& values, float t);
};

// Computes final skinning matrices (world-space joint matrix * inverseBindMatrix)
// for upload to the shader, given a set of local transforms (one per bone,
// already in TRS-composed mat4 form, indexed same as skeleton.bones).
void computeSkinningMatrices(
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& localTransforms,
    std::vector<glm::mat4>& outSkinningMatrices);

} // namespace blur
