#include "blur/mesh.hpp"

#include <glad/glad.h>

#define CGLTF_IMPLEMENTATION
#include "blur/external/cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "blur/external/stb_image.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace blur {

// ---------------------------------------------------------------------------
// Mesh
// ---------------------------------------------------------------------------

static void setupVertexAttribs() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));

    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, joints));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, weights));
}

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    m_indexCount = (unsigned int)indices.size();
    m_vertexCount = vertices.size();

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    setupVertexAttribs();

    glBindVertexArray(0);
}

Mesh::~Mesh() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
}

Mesh::Mesh(Mesh&& other) noexcept
    : transform(other.transform), isSkinned(other.isSkinned), textureIndex(other.textureIndex),
      m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo),
      m_indexCount(other.m_indexCount), m_vertexCount(other.m_vertexCount) {
    other.m_vao = other.m_vbo = other.m_ebo = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_ebo) glDeleteBuffers(1, &m_ebo);

        transform = other.transform;
        isSkinned = other.isSkinned;
        textureIndex = other.textureIndex;
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        m_vertexCount = other.m_vertexCount;

        other.m_vao = other.m_vbo = other.m_ebo = 0;
    }
    return *this;
}

void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::updateVertices(const std::vector<Vertex>& vertices) {
    if (vertices.size() != m_vertexCount) {
        std::fprintf(stderr, "Mesh::updateVertices: vertex count mismatch (%zu vs %zu)\n",
            vertices.size(), m_vertexCount);
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// glTF loading
// ---------------------------------------------------------------------------

namespace {

struct LoadedMeshData {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    glm::mat4 transform;
    bool isSkinned;
    int textureIndex = -1;
};

glm::mat4 nodeLocalTransform(cgltf_node* node) {
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

void loadMeshPrimitives(cgltf_data* data, cgltf_node* node, cgltf_mesh* mesh,
                         const glm::mat4& transform, std::vector<LoadedMeshData>& outMeshes,
                         const std::unordered_map<cgltf_image*, int>& imageToTexture) {
    cgltf_skin* skin = node->skin;

    size_t jointCount = skin ? skin->joints_count : 0;

    for (size_t p = 0; p < mesh->primitives_count; p++) {
        cgltf_primitive* prim = &mesh->primitives[p];
        if (prim->type != cgltf_primitive_type_triangles)
            continue;

        cgltf_accessor* posAcc = nullptr;
        cgltf_accessor* normAcc = nullptr;
        cgltf_accessor* uvAcc = nullptr;
        cgltf_accessor* jointsAcc = nullptr;
        cgltf_accessor* weightsAcc = nullptr;

        for (size_t a = 0; a < prim->attributes_count; a++) {
            cgltf_attribute* attr = &prim->attributes[a];
            switch (attr->type) {
                case cgltf_attribute_type_position: posAcc = attr->data; break;
                case cgltf_attribute_type_normal: normAcc = attr->data; break;
                case cgltf_attribute_type_texcoord:
                    if (attr->index == 0) uvAcc = attr->data;
                    break;
                case cgltf_attribute_type_joints:
                    if (attr->index == 0) jointsAcc = attr->data;
                    break;
                case cgltf_attribute_type_weights:
                    if (attr->index == 0) weightsAcc = attr->data;
                    break;
                default: break;
            }
        }

        if (!posAcc) continue;

        LoadedMeshData md;
        md.transform = transform;
        md.isSkinned = (jointsAcc != nullptr && weightsAcc != nullptr && skin != nullptr);

        cgltf_material* mat = prim->material;
        if (mat && mat->has_pbr_metallic_roughness &&
            mat->pbr_metallic_roughness.base_color_texture.texture) {
            cgltf_image* img = mat->pbr_metallic_roughness.base_color_texture.texture->image;
            auto it = imageToTexture.find(img);
            if (it != imageToTexture.end())
                md.textureIndex = it->second;
        }

        size_t vertCount = posAcc->count;
        md.vertices.resize(vertCount);

        for (size_t i = 0; i < vertCount; i++) {
            Vertex& v = md.vertices[i];

            cgltf_accessor_read_float(posAcc, i, &v.position.x, 3);

            if (normAcc)
                cgltf_accessor_read_float(normAcc, i, &v.normal.x, 3);
            else
                v.normal = glm::vec3(0, 1, 0);

            if (uvAcc)
                cgltf_accessor_read_float(uvAcc, i, &v.uv.x, 2);
            else
                v.uv = glm::vec2(0.0f);

            if (md.isSkinned) {
                cgltf_uint joints[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(jointsAcc, i, joints, 4);

                float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(weightsAcc, i, weights, 4);

                for (int k = 0; k < 4; k++) {
                    v.joints[k] = (joints[k] < jointCount) ? (int)joints[k] : 0;
                }
                v.weights = glm::vec4(weights[0], weights[1], weights[2], weights[3]);

                float wsum = v.weights.x + v.weights.y + v.weights.z + v.weights.w;
                if (wsum > 1e-6f) v.weights /= wsum;
            } else {
                v.joints = glm::ivec4(0);
                v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }

        if (prim->indices) {
            md.indices.resize(prim->indices->count);
            for (size_t i = 0; i < prim->indices->count; i++)
                md.indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
        } else {
            md.indices.resize(vertCount);
            for (size_t i = 0; i < vertCount; i++)
                md.indices[i] = (unsigned int)i;
        }

        outMeshes.push_back(std::move(md));
    }
}

void walkNode(cgltf_data* data, cgltf_node* node, const glm::mat4& parentTransform,
              std::vector<LoadedMeshData>& outMeshes,
              const std::unordered_map<cgltf_image*, int>& imageToTexture) {
    glm::mat4 worldTransform = parentTransform * nodeLocalTransform(node);

    if (node->mesh)
        loadMeshPrimitives(data, node, node->mesh, worldTransform, outMeshes, imageToTexture);

    for (size_t i = 0; i < node->children_count; i++)
        walkNode(data, node->children[i], worldTransform, outMeshes, imageToTexture);
}

bool buildSkeleton(cgltf_data* data, Skeleton& outSkeleton,
                    std::unordered_map<cgltf_node*, int>& outNodeToBone) {
    if (data->skins_count == 0) return false;

    cgltf_skin* skin = &data->skins[0];
    outSkeleton.bones.resize(skin->joints_count);

    for (size_t i = 0; i < skin->joints_count; i++) {
        cgltf_node* jointNode = skin->joints[i];
        outNodeToBone[jointNode] = (int)i;
    }

    for (size_t i = 0; i < skin->joints_count; i++) {
        cgltf_node* jointNode = skin->joints[i];
        Bone& bone = outSkeleton.bones[i];

        bone.name = jointNode->name ? jointNode->name : ("bone_" + std::to_string(i));

        bone.parentIndex = -1;
        if (jointNode->parent) {
            auto it = outNodeToBone.find(jointNode->parent);
            if (it != outNodeToBone.end())
                bone.parentIndex = it->second;
        }

        if (jointNode->has_translation)
            bone.bindTranslation = glm::vec3(jointNode->translation[0], jointNode->translation[1], jointNode->translation[2]);
        if (jointNode->has_rotation)
            bone.bindRotation = glm::quat(jointNode->rotation[3], jointNode->rotation[0], jointNode->rotation[1], jointNode->rotation[2]);
        if (jointNode->has_scale)
            bone.bindScale = glm::vec3(jointNode->scale[0], jointNode->scale[1], jointNode->scale[2]);

        if (skin->inverse_bind_matrices) {
            float m[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, i, m, 16);
            glm::mat4 ibm;
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++)
                    ibm[c][r] = m[c * 4 + r];
            bone.inverseBindMatrix = ibm;
        }
    }

    return true;
}

void buildAnimations(cgltf_data* data, std::unordered_map<cgltf_node*, int>& nodeToBone,
                      std::vector<Animation>& outAnimations) {
    outAnimations.reserve(data->animations_count);

    for (size_t a = 0; a < data->animations_count; a++) {
        cgltf_animation* srcAnim = &data->animations[a];

        Animation anim;
        anim.name = srcAnim->name ? srcAnim->name : ("anim_" + std::to_string(a));

        std::unordered_map<int, BoneTrack> tracksByBone;

        for (size_t c = 0; c < srcAnim->channels_count; c++) {
            cgltf_animation_channel* channel = &srcAnim->channels[c];
            if (!channel->target_node) continue;

            auto boneIt = nodeToBone.find(channel->target_node);
            if (boneIt == nodeToBone.end()) continue;

            int boneIndex = boneIt->second;
            BoneTrack& track = tracksByBone[boneIndex];
            track.boneIndex = boneIndex;

            cgltf_animation_sampler* sampler = channel->sampler;
            cgltf_accessor* timeAcc = sampler->input;
            cgltf_accessor* valueAcc = sampler->output;

            size_t keyCount = timeAcc->count;

            std::vector<float> times(keyCount);
            for (size_t k = 0; k < keyCount; k++) {
                float t;
                cgltf_accessor_read_float(timeAcc, k, &t, 1);
                times[k] = t;
            }

            float maxTime = times.empty() ? 0.0f : times.back();
            anim.duration = std::max(anim.duration, maxTime);

            switch (channel->target_path) {
                case cgltf_animation_path_type_translation: {
                    track.translationTimes = times;
                    track.translations.resize(keyCount);
                    for (size_t k = 0; k < keyCount; k++) {
                        float v[3];
                        cgltf_accessor_read_float(valueAcc, k, v, 3);
                        track.translations[k] = glm::vec3(v[0], v[1], v[2]);
                    }
                    break;
                }
                case cgltf_animation_path_type_rotation: {
                    track.rotationTimes = times;
                    track.rotations.resize(keyCount);
                    for (size_t k = 0; k < keyCount; k++) {
                        float v[4];
                        cgltf_accessor_read_float(valueAcc, k, v, 4);
                        track.rotations[k] = glm::quat(v[3], v[0], v[1], v[2]);
                    }
                    break;
                }
                case cgltf_animation_path_type_scale: {
                    track.scaleTimes = times;
                    track.scales.resize(keyCount);
                    for (size_t k = 0; k < keyCount; k++) {
                        float v[3];
                        cgltf_accessor_read_float(valueAcc, k, v, 3);
                        track.scales[k] = glm::vec3(v[0], v[1], v[2]);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        anim.tracks.reserve(tracksByBone.size());
        for (auto& kv : tracksByBone)
            anim.tracks.push_back(std::move(kv.second));

        outAnimations.push_back(std::move(anim));
    }
}

} // namespace

bool loadModel(const std::string& path, Model& outModel) {
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

    std::unordered_map<cgltf_node*, int> nodeToBone;
    bool hasSkin = buildSkeleton(data, outModel.skeleton, nodeToBone);

    if (hasSkin) {
        buildAnimations(data, nodeToBone, outModel.animations);
    }

    std::string gltfDir;
    size_t slashPos = path.find_last_of("/\\");
    if (slashPos != std::string::npos) gltfDir = path.substr(0, slashPos);

    std::unordered_map<cgltf_image*, int> imageToTexture;
    imageToTexture.reserve(data->images_count);
    outModel.textures.reserve(data->images_count);

    for (size_t i = 0; i < data->images_count; i++) {
        cgltf_image* img = &data->images[i];
        int w = 0, h = 0, channels = 0;
        unsigned char* decoded = nullptr;

        if (img->buffer_view) {
            cgltf_buffer_view* bv = img->buffer_view;
            const unsigned char* bytes = (const unsigned char*)bv->buffer->data + bv->offset;
            decoded = stbi_load_from_memory(bytes, (int)bv->size, &w, &h, &channels, 4);
        } else if (img->uri) {
            std::string imgPath = gltfDir.empty() ? img->uri : (gltfDir + "/" + img->uri);
            decoded = stbi_load(imgPath.c_str(), &w, &h, &channels, 4);
        }

        if (decoded) {
            outModel.textures.push_back(Texture::fromPixels(decoded, w, h));
            imageToTexture[img] = (int)outModel.textures.size() - 1;
            stbi_image_free(decoded);
        } else {
            std::fprintf(stderr, "Failed to decode image %zu (%s)\n", i, img->uri ? img->uri : "<embedded>");
        }
    }

    std::vector<LoadedMeshData> loaded;
    for (size_t s = 0; s < data->scenes_count; s++) {
        cgltf_scene* scene = &data->scenes[s];
        for (size_t n = 0; n < scene->nodes_count; n++)
            walkNode(data, scene->nodes[n], glm::mat4(1.0f), loaded, imageToTexture);
    }

    cgltf_free(data);

    outModel.meshes.reserve(loaded.size());
    outModel.meshVertices.reserve(loaded.size());
    for (auto& md : loaded) {
        Mesh mesh(md.vertices, md.indices);
        mesh.transform = md.transform;
        mesh.isSkinned = md.isSkinned;
        mesh.textureIndex = md.textureIndex;
        outModel.meshVertices.push_back(md.vertices);
        outModel.meshIndices.push_back(md.indices);
        outModel.meshes.push_back(std::move(mesh));

    }

    return true;
}

bool loadGltf(const std::string& path, std::vector<Mesh>& outMeshes) {
    Model model;
    if (!loadModel(path, model)) return false;
    outMeshes = std::move(model.meshes);
    return true;
}

// ---------------------------------------------------------------------------
// AnimatedModel
// ---------------------------------------------------------------------------

AnimatedModel::AnimatedModel(Model model) : m_model(std::move(model)) {
    if (hasSkeleton()) {
        m_localTransforms.resize(m_model.skeleton.bones.size());
        m_skinningMatrices.resize(m_model.skeleton.bones.size(), glm::mat4(1.0f));
    }

    m_originalVertices = m_model.meshVertices;
    m_workingVertices = m_model.meshVertices;
}

void AnimatedModel::playAnimation(const std::string& name, bool loop, float blendDuration) {
    int idx = m_model.findAnimation(name);
    if (idx < 0) {
        std::fprintf(stderr, "AnimatedModel: animation '%s' not found\n", name.c_str());
        return;
    }
    playAnimation(idx, loop, blendDuration);
}

void AnimatedModel::playAnimation(int index, bool loop, float blendDuration) {
    if (index < 0 || index >= (int)m_model.animations.size()) return;
    if (index == m_currentAnimation) { m_looping = loop; return; }

    if (m_currentAnimation >= 0 && blendDuration > 0.0f) {
        m_previousAnimation = m_currentAnimation;
        m_prevAnimTime      = m_animTime;
        m_prevLooping       = m_looping;
        m_blendElapsed      = 0.0f;
        m_blendDuration      = blendDuration;
    } else {
        m_previousAnimation = -1;
    }

    m_currentAnimation = index;
    m_animTime = 0.0f;
    m_looping = loop;
}

void AnimatedModel::update(float deltaTime) {
    m_wobbleTime += deltaTime;

    bool blending = m_previousAnimation >= 0 && hasSkeleton();

    if (m_currentAnimation >= 0 && hasSkeleton()) {
        const Animation& anim = m_model.animations[m_currentAnimation];
        m_animTime += deltaTime;
        if (anim.duration > 0.0f) {
            if (m_looping) m_animTime = std::fmod(m_animTime, anim.duration);
            else if (m_animTime > anim.duration) m_animTime = anim.duration;
        }
        anim.sample(m_animTime, m_model.skeleton, m_localTransforms);

        if (blending) {
            const Animation& prevAnim = m_model.animations[m_previousAnimation];
            m_prevAnimTime += deltaTime;
            if (prevAnim.duration > 0.0f) {
                if (m_prevLooping) m_prevAnimTime = std::fmod(m_prevAnimTime, prevAnim.duration);
                else if (m_prevAnimTime > prevAnim.duration) m_prevAnimTime = prevAnim.duration;
            }
            prevAnim.sample(m_prevAnimTime, m_model.skeleton, m_prevLocalTransforms);

            m_blendElapsed += deltaTime;
            float alpha = glm::clamp(m_blendElapsed / m_blendDuration, 0.0f, 1.0f);

            size_t count = m_localTransforms.size();
            m_blendedLocal.resize(count);
            for (size_t i = 0; i < count; i++) {
                const glm::mat4& a = (i < m_prevLocalTransforms.size()) ? m_prevLocalTransforms[i] : m_localTransforms[i];
                const glm::mat4& b = m_localTransforms[i];
                glm::mat4 r;
                for (int c = 0; c < 4; c++) r[c] = glm::mix(a[c], b[c], alpha);
                m_blendedLocal[i] = r;
            }

            computeSkinningMatrices(m_model.skeleton, m_blendedLocal, m_skinningMatrices);
            if (alpha >= 1.0f) m_previousAnimation = -1;
        } else {
            computeSkinningMatrices(m_model.skeleton, m_localTransforms, m_skinningMatrices);
        }
    }

    if (m_wobbleEnabled || m_customEditFn) {
        applyVertexEdits();
    }
}
void AnimatedModel::stopAnimation() {
    m_currentAnimation = -1;
}



void AnimatedModel::applyVertexEdits() {
    for (size_t m = 0; m < m_model.meshes.size(); m++) {
        if (m >= m_originalVertices.size() || m >= m_workingVertices.size()) continue;

        const auto& original = m_originalVertices[m];
        auto& working = m_workingVertices[m];

        if (m_customEditFn) {
            m_customEditFn(original, working, m_wobbleTime, m_customEditUserData);
        } else if (m_wobbleEnabled) {
            for (size_t i = 0; i < original.size(); i++) {
                const Vertex& src = original[i];
                Vertex& dst = working[i];
                dst = src;

                float phase = src.position.x * 2.0f + src.position.y * 1.3f;
                float offset = std::sin(m_wobbleTime * m_wobbleFrequency + phase) * m_wobbleAmplitude;
                dst.position += src.normal * offset;
            }
        } else {
            continue;
        }

        m_model.meshes[m].updateVertices(working);
    }
}

void AnimatedModel::draw() const {
    for (auto& mesh : m_model.meshes) {
        mesh.draw();
    }
}

void AnimatedModel::setWobble(bool enabled, float amplitude, float frequency) {
    m_wobbleEnabled = enabled;
    m_wobbleAmplitude = amplitude;
    m_wobbleFrequency = frequency;
    m_customEditFn = nullptr;
}

void AnimatedModel::setCustomVertexEditor(VertexEditFn fn, void* userData) {
    m_customEditFn = fn;
    m_customEditUserData = userData;
    m_wobbleEnabled = false;
}

void AnimatedModel::getBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());

    for (size_t m = 0; m < m_model.meshes.size(); m++) {
        if (m >= m_originalVertices.size()) continue;
        const Mesh& mesh = m_model.meshes[m];

        for (const Vertex& v : m_originalVertices[m]) {
            glm::vec3 p = mesh.isSkinned
                ? v.position
                : glm::vec3(mesh.transform * glm::vec4(v.position, 1.0f));
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
        }
    }

    outMin = mn;
    outMax = mx;
}

} // namespace blur
