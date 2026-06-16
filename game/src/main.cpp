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
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

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

namespace {

// ---------------------------------------------------------------------------
// Player movement tuning
// ---------------------------------------------------------------------------
// Car-like steering model: the stick doesn't set velocity directly. Instead
// it picks a "wish point" a fixed look-ahead distance in front of Sonic
// (in camera space), and Sonic curves his forward vector toward that point
// at a turn rate that depends on current speed (tighter turns at low speed,
// wider sweeping arcs at high speed - same reason cars understeer at speed).
// Forward speed itself is a separate scalar with its own accel/decel/brake,
// so stopping and turning are independent like a platformer, not a strafe.
struct MoveTuning {
    float lookAheadDist   = 4.0f;   // how far ahead the wish point sits
    float maxSpeed         = 16.0f;  // top ground speed
    float accel             = 22.0f;  // speed gain per second when holding a direction
    float decel             = 14.0f;  // speed loss per second when stick is neutral
    float brakeDecel        = 36.0f;  // speed loss per second when steering hard against current motion
    float minTurnRate       = 5.5f;   // rad/s turn rate at zero speed (sharp pivot)
    float maxTurnRate       = 1.1f;   // rad/s turn rate at max speed (wide arc)
    float slopeAccelScale   = 6.0f;   // extra accel per second from downhill grade
    float wallTiltBase      = 0.62f;  // rad (~35.5deg) max normal-change tolerated at zero speed
    float wallTiltPerSpeed  = 0.022f; // extra rad of tolerance per unit of speed
    float wallTiltMax       = 1.25f;  // rad (~71.6deg) absolute cap regardless of speed
    float upEase            = 10.0f;  // how fast currentUp eases toward target up
};

constexpr MoveTuning kTune;

float wrapAngle(float a) {
    while (a > glm::pi<float>()) a -= glm::two_pi<float>();
    while (a < -glm::pi<float>()) a += glm::two_pi<float>();
    return a;
}

} // namespace

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
    float speed = 0.0f; // signed scalar along `forward`

    // Tracks where we are along the debug track so closestArcLength has a
    // cheap hint to search from instead of scanning every node every frame.
    float playerArc = 0.0f;

    // Camera yaw trails the player's facing instead of being locked to a
    // track frame or world axis, so it settles in behind Sonic as he turns.
    float camYaw = 0.0f;

    if (!debugMode) playerPos = glm::vec3(0.0f, 1.0f, 0.0f);

    double lastTime = glfwGetTime();

    while (!window.shouldClose()) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;
        dt = glm::clamp(dt, 0.0f, 1.0f / 30.0f); // avoid huge steps on hitches

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
        float stickMag = glm::length(stick);

        if (debugMode) {
            track.update(playerArc, 120.0f);
            track.trim(playerArc, 60.0f);
        }

        // --- Wish position: camera-relative point a few units ahead -------
        glm::vec3 camForward = glm::normalize(glm::vec3(std::sin(camYaw), 0.0f, std::cos(camYaw)));
        glm::vec3 camRight = glm::normalize(glm::cross(currentUp, camForward));
        glm::vec3 stickDirWorld = camForward * stick.y + camRight * stick.x;

        glm::vec3 wishPos = playerPos + stickDirWorld * kTune.lookAheadDist;

        // --- Steering: curve `forward` toward the wish point, car-style ----
        glm::vec3 toWish = wishPos - playerPos;
        toWish -= currentUp * glm::dot(toWish, currentUp); // project onto ground plane

        if (stickMag > 1e-3f && glm::length(toWish) > 1e-4f) {
            glm::vec3 toWishN = glm::normalize(toWish);
            glm::vec3 fwdFlat = glm::normalize(forward - currentUp * glm::dot(forward, currentUp));

            float targetYaw = std::atan2(toWishN.x, toWishN.z);
            float curYaw = std::atan2(fwdFlat.x, fwdFlat.z);
            float diff = wrapAngle(targetYaw - curYaw);

            // Faster = wider arcs (lower turn rate), like understeer.
            float speedT = glm::clamp(std::fabs(speed) / kTune.maxSpeed, 0.0f, 1.0f);
            float turnRate = glm::mix(kTune.minTurnRate, kTune.maxTurnRate, speedT);

            float maxStep = turnRate * dt;
            float step = glm::clamp(diff, -maxStep, maxStep);
            float newYaw = curYaw + step;
            forward = glm::vec3(std::sin(newYaw), 0.0f, std::cos(newYaw));
        }

        // --- Accel / decel / brake -----------------------------------------
        // Braking kicks in when the stick wants a direction that's mostly
        // opposite our current heading - otherwise turning sharply while
        // fast would never feel like it costs you speed.
        float headingDot = 0.0f;
        if (stickMag > 1e-3f) {
            glm::vec3 fwdFlat = glm::normalize(forward - currentUp * glm::dot(forward, currentUp));
            glm::vec3 wishFlat = glm::normalize(stickDirWorld - currentUp * glm::dot(stickDirWorld, currentUp));
            headingDot = glm::dot(fwdFlat, wishFlat);
        }

        if (stickMag > 0.05f) {
            if (headingDot < -0.3f && speed > 1.0f) {
                speed -= kTune.brakeDecel * dt;
            } else {
                speed += kTune.accel * stickMag * dt;
            }
        } else {
            float decel = kTune.decel * dt;
            speed = (speed > 0.0f) ? std::max(0.0f, speed - decel)
                                    : std::min(0.0f, speed + decel);
        }
        speed = glm::clamp(speed, -kTune.maxSpeed * 0.5f, kTune.maxSpeed);

        // --- Slope assist: gain speed downhill, lose it uphill --------------
        glm::vec3 fwdFlatForSlope = forward - currentUp * glm::dot(forward, currentUp);
        if (glm::length(fwdFlatForSlope) > 1e-5f) {
            // forward.y relative to currentUp tells us the grade; using the
            // raw world-space tilt of forward vs up is enough for a debug feel.
            float grade = glm::dot(glm::normalize(forward), glm::vec3(0, -1, 0)); // >0 means pointing downhill
            speed += grade * kTune.slopeAccelScale * dt;
            speed = glm::clamp(speed, -kTune.maxSpeed * 0.5f, kTune.maxSpeed);
        }

        glm::vec3 velocity = forward * speed;
        glm::vec3 nextPos = playerPos + velocity * dt;

        // --- Predictive tilt check: would committing to this path's wall ---
        // angle exceed our threshold? If so, treat it like hitting a wall
        // (kill the curve-up, just stop gaining tilt) instead of driving up
        // it. Threshold scales with speed so fast Sonic can commit to banks
        // that would just be a wall at a standstill.
        glm::vec3 targetUp = currentUp;
        bool hitWall = false;
        constexpr float kWallImpactDecel = 0.85f; // fraction of speed kept after a hard wall hit

        if (debugMode) {
            playerArc = track.closestArcLength(playerPos, playerArc);
            float lookAheadArc = playerArc + glm::max(1.0f, std::fabs(speed) * 0.25f);
            debugmode::TrackFrame here = track.sampleFrame(playerArc);
            debugmode::TrackFrame ahead = track.sampleFrame(lookAheadArc);

            float tiltAngle = std::acos(glm::clamp(glm::dot(here.up, ahead.up), -1.0f, 1.0f));
            float threshold = glm::min(kTune.wallTiltBase + kTune.wallTiltPerSpeed * std::fabs(speed), kTune.wallTiltMax);

            if (tiltAngle <= threshold) {
                targetUp = ahead.up;
            } else {
                // Wall is too steep relative to our speed: don't tilt up
                // into it, and bleed off the part of velocity driving into
                // the wall so we "hit" it rather than climb it.
                hitWall = true;
                targetUp = here.up;

                glm::vec3 wallNormal = glm::normalize(ahead.up - here.up * glm::dot(ahead.up, here.up));
                if (glm::length(wallNormal) > 1e-4f) {
                    float into = glm::dot(velocity, wallNormal);
                    if (into > 0.0f) {
                        velocity -= wallNormal * into;
                        velocity *= kWallImpactDecel;
                        speed = glm::length(velocity) * (glm::dot(velocity, forward) < 0.0f ? -1.0f : 1.0f);
                    }
                }
                nextPos = playerPos + velocity * dt;
            }

            // Snap onto the track's ground height/lateral bounds so we
            // actually run ON it rather than floating over/through it.
            debugmode::TrackFrame finalHere = track.sampleFrame(track.closestArcLength(nextPos, playerArc));
            glm::vec3 toPos = nextPos - finalHere.position;
            float lateral = glm::dot(toPos, finalHere.right);
            float halfWidth = track.width() * 0.5f - 0.5f;
            lateral = glm::clamp(lateral, -halfWidth, halfWidth);

            float forwardOnTrack = glm::dot(toPos, finalHere.tangent);
            nextPos = finalHere.position + finalHere.right * lateral + finalHere.tangent * forwardOnTrack;
            // Re-flatten onto the surface (cancel any drift along up).
            nextPos -= finalHere.up * glm::dot(nextPos - finalHere.position, finalHere.up);
        } else if (worldLoaded && !worldCollision.empty()) {
            blur::CollisionHit hit;
            glm::vec3 probeOrigin = nextPos + currentUp * 5.0f;
            if (worldCollision.raycast(probeOrigin, -currentUp, 50.0f, hit)) {
                float tiltAngle = std::acos(glm::clamp(glm::dot(currentUp, hit.normal), -1.0f, 1.0f));
                float threshold = glm::min(kTune.wallTiltBase + kTune.wallTiltPerSpeed * std::fabs(speed), kTune.wallTiltMax);

                if (tiltAngle <= threshold) {
                    nextPos = hit.point;
                    targetUp = hit.normal;
                } else {
                    hitWall = true;
                    glm::vec3 wallNormal = glm::normalize(hit.normal - currentUp * glm::dot(hit.normal, currentUp));
                    if (glm::length(wallNormal) > 1e-4f) {
                        float into = glm::dot(velocity, wallNormal);
                        if (into > 0.0f) {
                            velocity -= wallNormal * into;
                            velocity *= kWallImpactDecel;
                            speed = glm::length(velocity) * (glm::dot(velocity, forward) < 0.0f ? -1.0f : 1.0f);
                        }
                    }
                    nextPos = playerPos + velocity * dt;
                }
            }
        }

        playerPos = nextPos;
        float upEaseRate = hitWall ? 1e6f : kTune.upEase; // snap upright on impact instead of easing
        currentUp = glm::normalize(glm::mix(currentUp, targetUp, glm::clamp(upEaseRate * dt, 0.0f, 1.0f)));

        // Re-orthogonalize the basis against the (possibly tilted) up.
        forward = glm::normalize(forward - currentUp * glm::dot(forward, currentUp));
        right = glm::normalize(glm::cross(currentUp, forward));
        forward = glm::cross(right, currentUp);

        // Camera eases its yaw toward the player's facing so it settles in
        // behind them as they move/turn.
        if (stickMag > 1e-3f || std::fabs(speed) > 0.5f) {
            float facingYaw = std::atan2(forward.x, forward.z);
            float diff = wrapAngle(facingYaw - camYaw);
            camYaw += diff * glm::clamp(6.0f * dt, 0.0f, 1.0f);
        }

        glm::mat4 basis(
            glm::vec4(right, 0.0f),
            glm::vec4(currentUp, 0.0f),
            glm::vec4(forward, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        animModel.update(dt);
        renderer.clear();

        glm::vec3 camDir = glm::normalize(glm::vec3(std::sin(camYaw), 0.0f, std::cos(camYaw)));
        glm::vec3 camEye = playerPos - camDir * 6.0f + currentUp * 3.0f;
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
