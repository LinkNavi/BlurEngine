#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

#include "blur/skeleton.hpp"

namespace blur {

constexpr int kMaxBoneInfluences = 4;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    // Skinning data. If a mesh has no skin, joints are all 0 and weights[0] = 1
    // (i.e. fully bound to bone 0 / identity), so the same shader path works
    // for skinned and unskinned meshes.
    glm::ivec4 joints = glm::ivec4(0);
    glm::vec4 weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
};

class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();

    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;

    // Re-uploads vertex data (positions/normals/etc). Used for CPU-side
    // wobble/deformation where you mutate a copy of the original vertices
    // and push it back to the GPU each frame.
    void updateVertices(const std::vector<Vertex>& vertices);

    glm::mat4 transform = glm::mat4(1.0f);

    // True if this mesh has real skin weights (vs. the identity-bound default).
    bool isSkinned = false;

private:
    unsigned int m_vao = 0, m_vbo = 0, m_ebo = 0;
    unsigned int m_indexCount = 0;
    size_t m_vertexCount = 0;
};

// A loaded model: meshes + (optionally) a skeleton + animations, all indexed
// against the same skeleton. If the source glTF had no skin, skeleton.bones
// is empty and animations will be empty too.
struct Model {
    std::vector<Mesh> meshes;

    // CPU copy of each mesh's bind-pose vertices, same order as `meshes`.
    // Kept around specifically so AnimatedModel can re-deform from a clean
    // base each frame (wobble, skinning preview, etc) without needing a
    // GPU readback.
    std::vector<std::vector<Vertex>> meshVertices;

    Skeleton skeleton;
    std::vector<Animation> animations;

    int findAnimation(const std::string& name) const {
        for (size_t i = 0; i < animations.size(); i++)
            if (animations[i].name == name) return (int)i;
        return -1;
    }
};

// Loads a glTF/glb file (model + skin + animations if present).
// Returns false on failure.
bool loadModel(const std::string& path, Model& outModel);

// Convenience wrapper kept for existing callers that only want meshes
// (no skinning/animation). Internally calls loadModel and discards the rest.
bool loadGltf(const std::string& path, std::vector<Mesh>& outMeshes);

// A vertex-editing callback used for procedural effects (e.g. "wobble").
// Receives the *original* unmodified vertices and writes the deformed result
// into outVertices (same size). Called once per frame by AnimatedModel::update
// if a wobble function is set.
using VertexEditFn = void(*)(const std::vector<Vertex>& original, std::vector<Vertex>& outVertices, float time, void* userData);

// Drives a Model's animation playback and optional per-vertex wobble effect.
// Keeps its own copy of the source vertex data for each mesh so it can
// re-deform from the original each frame without compounding distortion.
class AnimatedModel {
public:
    explicit AnimatedModel(Model model);

    void playAnimation(const std::string& name, bool loop = true);
    void playAnimation(int index, bool loop = true);
    void stopAnimation();

    // Advances animation time and (if enabled) re-applies the wobble effect.
    // Call once per frame before draw().
    void update(float deltaTime);

    void draw() const;

    // Enables/disables the wobble effect and sets its parameters.
    // amplitude: max positional offset in model units.
    // frequency: oscillations per second.
    void setWobble(bool enabled, float amplitude = 0.05f, float frequency = 4.0f);

    // For fully custom deformation, bypass the built-in sine wobble.
    void setCustomVertexEditor(VertexEditFn fn, void* userData = nullptr);

    const std::vector<glm::mat4>& boneMatrices() const { return m_skinningMatrices; }
    bool hasSkeleton() const { return !m_model.skeleton.bones.empty(); }

private:
    void applyVertexEdits();

    Model m_model;

    // Original (bind-pose) vertex data per mesh, kept so wobble/edits don't
    // accumulate across frames.
    std::vector<std::vector<Vertex>> m_originalVertices;
    std::vector<std::vector<Vertex>> m_workingVertices;

    int m_currentAnimation = -1;
    float m_animTime = 0.0f;
    bool m_looping = true;

    std::vector<glm::mat4> m_localTransforms;
    std::vector<glm::mat4> m_skinningMatrices;

    bool m_wobbleEnabled = false;
    float m_wobbleAmplitude = 0.05f;
    float m_wobbleFrequency = 4.0f;
    float m_wobbleTime = 0.0f;

    VertexEditFn m_customEditFn = nullptr;
    void* m_customEditUserData = nullptr;
};

} // namespace blur
