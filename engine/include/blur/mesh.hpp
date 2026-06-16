#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

#include "blur/skeleton.hpp"
#include "blur/texture.hpp"

namespace blur {

constexpr int kMaxBoneInfluences = 4;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

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

    void updateVertices(const std::vector<Vertex>& vertices);

    glm::mat4 transform = glm::mat4(1.0f);

    bool isSkinned = false;
    int textureIndex = -1; // index into Model::textures, -1 = none

private:
    unsigned int m_vao = 0, m_vbo = 0, m_ebo = 0;
    unsigned int m_indexCount = 0;
    size_t m_vertexCount = 0;
};

struct Model {
    std::vector<Mesh> meshes;
    std::vector<std::vector<Vertex>> meshVertices;
    std::vector<std::vector<unsigned int>> meshIndices;
    std::vector<Texture> textures;

    Skeleton skeleton;
    std::vector<Animation> animations;

    int findAnimation(const std::string& name) const {
        for (size_t i = 0; i < animations.size(); i++)
            if (animations[i].name == name) return (int)i;
        return -1;
    }
};

bool loadModel(const std::string& path, Model& outModel);

bool loadGltf(const std::string& path, std::vector<Mesh>& outMeshes);

using VertexEditFn = void(*)(const std::vector<Vertex>& original, std::vector<Vertex>& outVertices, float time, void* userData);

class AnimatedModel {
public:
    explicit AnimatedModel(Model model);

    void playAnimation(const std::string& name, bool loop = true, float blendDuration = 0.15f);
    void playAnimation(int index, bool loop = true, float blendDuration = 0.15f);
    void stopAnimation();

    void update(float deltaTime);

    void draw() const;

    void setWobble(bool enabled, float amplitude = 0.05f, float frequency = 4.0f);

    void setCustomVertexEditor(VertexEditFn fn, void* userData = nullptr);

    const std::vector<glm::mat4>& boneMatrices() const { return m_skinningMatrices; }
    bool hasSkeleton() const { return !m_model.skeleton.bones.empty(); }

    const std::vector<Mesh>& meshes() const { return m_model.meshes; }
    const std::vector<Animation>& animations() const { return m_model.animations; }
    const std::vector<Texture>& textures() const { return m_model.textures; }

    // Approximate world-space bbox from bind-pose vertices. Skinned meshes
    // ignore their own node transform per glTF spec; unskinned meshes use it.
    void getBounds(glm::vec3& outMin, glm::vec3& outMax) const;

private:
    void applyVertexEdits();

    Model m_model;

    int m_previousAnimation = -1;
    float m_prevAnimTime = 0.0f;
    bool m_prevLooping = true;
    float m_blendElapsed = 0.0f;
    float m_blendDuration = 0.15f;
    std::vector<glm::mat4> m_prevLocalTransforms;
    std::vector<glm::mat4> m_blendedLocal;

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
