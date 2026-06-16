#define GLM_ENABLE_EXPERIMENTAL

#include "blur/window.hpp"
#include "blur/renderer.hpp"
#include "blur/shader.hpp"
#include "blur/mesh.hpp"
#include "blur/gamepad.hpp"
#include "blur/collision.hpp"
#include "blur/texture.hpp"

#include "debug/noise.hpp"
#include "debug/track.hpp"

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>

static const char* kVertSrc = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

const int MAX_BONES = 128;
uniform mat4 uBoneMatrices[MAX_BONES];
uniform bool uIsSkinned;

out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 skinnedPos = vec4(aPos, 1.0);
    vec3 skinnedNormal = aNormal;

    if (uIsSkinned) {
        mat4 skinMat =
            uBoneMatrices[aJoints.x] * aWeights.x +
            uBoneMatrices[aJoints.y] * aWeights.y +
            uBoneMatrices[aJoints.z] * aWeights.z +
            uBoneMatrices[aJoints.w] * aWeights.w;

        skinnedPos = skinMat * vec4(aPos, 1.0);
        skinnedNormal = mat3(skinMat) * aNormal;
    }

    vNormal = mat3(uModel) * skinnedNormal;
    vUV = aUV;
    gl_Position = uProj * uView * uModel * skinnedPos;
}
)";

// Key + fill + ambient. Cheap, easy swap point for real lighting later.
static const char* kFragSrc = R"(
#version 460 core
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;

uniform bool uHasTexture;
uniform sampler2D uTexture;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uFillDir;
uniform vec3 uFillColor;
uniform vec3 uAmbientColor;

void main() {
    vec3 n = normalize(vNormal);
    float keyDiff = max(dot(n, uLightDir), 0.0);
    float fillDiff = max(dot(n, uFillDir), 0.0);
    vec3 light = uAmbientColor + uLightColor * keyDiff + uFillColor * fillDiff;

    vec3 base = uHasTexture ? texture(uTexture, vUV).rgb : vec3(0.8, 0.8, 0.85);
    FragColor = vec4(base * light, 1.0);
}
)";

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    bool debugFlag = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--debug") debugFlag = true;
        else positional.push_back(arg);
    }

    std::string characterPath = positional.size() > 0 ? positional[0] : "";
    std::string mapPath = positional.size() > 1 ? positional[1] : "";
    bool debugMode = debugFlag || mapPath.empty();

    blur::Window window(1280, 720, "Blur Engine");
    blur::Renderer renderer;
    blur::Shader shader(kVertSrc, kFragSrc);
    blur::Gamepad pad;

    constexpr int kMaxBones = 128;
    std::vector<int> boneLocs(kMaxBones);
    for (int i = 0; i < kMaxBones; i++) {
        std::string name = "uBoneMatrices[" + std::to_string(i) + "]";
        boneLocs[i] = glGetUniformLocation(shader.id(), name.c_str());
    }

    shader.use();
    shader.setVec3("uLightDir", glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
    shader.setVec3("uLightColor", glm::vec3(0.85f));
    shader.setVec3("uFillDir", glm::normalize(glm::vec3(-0.4f, 0.3f, -0.5f)));
    shader.setVec3("uFillColor", glm::vec3(0.25f, 0.27f, 0.3f));
    shader.setVec3("uAmbientColor", glm::vec3(0.16f, 0.17f, 0.2f));

    blur::Model charModel;
    bool charLoaded = !characterPath.empty() && blur::loadModel(characterPath, charModel);
    if (!characterPath.empty() && !charLoaded)
        std::fprintf(stderr, "Failed to load character: %s\n", characterPath.c_str());

    blur::AnimatedModel animModel(std::move(charModel));
    if (charLoaded && !animModel.animations().empty())
        animModel.playAnimation(0, true);

    blur::Model worldModel;
    blur::CollisionMesh worldCollision;
    bool worldLoaded = false;
    if (!debugMode) {
        worldLoaded = blur::loadModel(mapPath, worldModel);
        if (worldLoaded) {
            worldCollision.build(worldModel);
        } else {
            std::fprintf(stderr, "Failed to load map: %s, falling back to debug mode\n", mapPath.c_str());
            debugMode = true;
        }
    }

    debugmode::EndlessTrack track(/*width=*/6.0f, /*segmentLength=*/2.0f, /*chunkLength=*/40.0f, /*seed=*/1337u);
    blur::Texture noiseTexture;
    if (debugMode) {
        auto pixels = debugmode::generateNoisePixels(256, 256, /*cellSize=*/24, /*seed=*/7u);
        noiseTexture = blur::Texture::fromPixels(pixels.data(), 256, 256);
        track.update(0.0f, 120.0f);
    }

    glm::vec3 playerPos(0.0f);
    glm::vec3 currentUp(0.0f, 1.0f, 0.0f);
    glm::vec3 forward(0.0f, 0.0f, 1.0f);
    glm::vec3 right(1.0f, 0.0f, 0.0f);

    float playerArc = 0.0f;
    float lateralOffset = 0.0f;
    const float forwardSpeed = 10.0f;
    const float steerSpeed = 6.0f;

    if (!debugMode) playerPos = glm::vec3(0.0f, 1.0f, 0.0f);

    double lastTime = glfwGetTime();

    while (!window.shouldClose()) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;

        window.pollEvents();
        SDL_PumpEvents();
        pad.update();

        glm::vec2 stick(0.0f);
        if (pad.isConnected()) {
            stick = pad.leftStick();
            window.setWindowTitle((std::string("Blur Engine - ") + pad.name()).c_str());
        } else {
            window.setWindowTitle("Blur Engine - No gamepad detected");
        }

        if (debugMode) {
            playerArc += forwardSpeed * dt;
            lateralOffset += stick.x * steerSpeed * dt;

            float halfWidth = track.width() * 0.5f - 0.6f;
            lateralOffset = glm::clamp(lateralOffset, -halfWidth, halfWidth);

            track.update(playerArc, 120.0f);
            track.trim(playerArc, 60.0f);

            debugmode::TrackFrame frame = track.sampleFrame(playerArc);
            playerPos = frame.position + frame.right * lateralOffset;
            currentUp = frame.up;
            forward = frame.tangent;
            right = frame.right;
        } else {
            playerPos.x += stick.x * forwardSpeed * dt;
            playerPos.z += stick.y * forwardSpeed * dt;

            if (worldLoaded && !worldCollision.empty()) {
                blur::CollisionHit hit;
                glm::vec3 probeOrigin = playerPos + currentUp * 5.0f;
                if (worldCollision.raycast(probeOrigin, -currentUp, 50.0f, hit)) {
                    playerPos = hit.point;
                    glm::vec3 from = glm::normalize(currentUp);
                    glm::vec3 to = glm::normalize(hit.normal);
                    if (glm::dot(from, to) < 0.9999f) {
                        float t = glm::clamp(10.0f * dt, 0.0f, 1.0f);
                        glm::quat rot = glm::rotation(from, to);
                        currentUp = glm::normalize(glm::slerp(glm::quat(1, 0, 0, 0), rot, t) * from);
                    }
                }
            }

            glm::vec3 stickWorld(stick.x, 0.0f, stick.y);
            glm::vec3 f = stickWorld - currentUp * glm::dot(stickWorld, currentUp);
            if (glm::length(f) > 1e-4f) forward = glm::normalize(f);
            right = glm::normalize(glm::cross(currentUp, forward));
            forward = glm::cross(right, currentUp);
        }

        glm::mat4 basis(
            glm::vec4(right, 0.0f),
            glm::vec4(currentUp, 0.0f),
            glm::vec4(forward, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        animModel.update(dt);
        renderer.clear();

        glm::vec3 camEye = playerPos - forward * 6.0f + currentUp * 3.0f;
        glm::vec3 camTarget = playerPos + currentUp * 1.2f;
        glm::mat4 view = glm::lookAt(camEye, camTarget, glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
            (float)window.width() / (float)window.height(), 0.05f, 500.0f);

        shader.use();
        shader.setMat4("uView", view);
        shader.setMat4("uProj", proj);

        const auto& bones = animModel.boneMatrices();
        int boneCount = (int)bones.size();
        for (int i = 0; i < boneCount && i < kMaxBones; i++)
            glUniformMatrix4fv(boneLocs[i], 1, GL_FALSE, glm::value_ptr(bones[i]));

        if (debugMode) {
            shader.setMat4("uModel", glm::mat4(1.0f));
            shader.setInt("uIsSkinned", 0);
            shader.setInt("uHasTexture", 1);
            noiseTexture.bind(0);
            shader.setInt("uTexture", 0);
            track.draw();
        } else if (worldLoaded) {
            const auto& worldTextures = worldModel.textures;
            for (const auto& mesh : worldModel.meshes) {
                shader.setMat4("uModel", mesh.transform);
                shader.setInt("uIsSkinned", 0);
                bool hasTex = mesh.textureIndex >= 0 && mesh.textureIndex < (int)worldTextures.size();
                shader.setInt("uHasTexture", hasTex ? 1 : 0);
                if (hasTex) {
                    worldTextures[mesh.textureIndex].bind(0);
                    shader.setInt("uTexture", 0);
                }
                mesh.draw();
            }
        }

        glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), playerPos) * basis;
        const auto& textures = animModel.textures();

        for (const auto& mesh : animModel.meshes()) {
            glm::mat4 meshModel = mesh.isSkinned ? modelMat : (modelMat * mesh.transform);
            shader.setMat4("uModel", meshModel);
            shader.setInt("uIsSkinned", mesh.isSkinned ? 1 : 0);

            bool hasTex = mesh.textureIndex >= 0 && mesh.textureIndex < (int)textures.size();
            shader.setInt("uHasTexture", hasTex ? 1 : 0);
            if (hasTex) {
                textures[mesh.textureIndex].bind(0);
                shader.setInt("uTexture", 0);
            }
            mesh.draw();
        }

        window.swapBuffers();
    }

    SDL_Quit();
    return 0;
}
