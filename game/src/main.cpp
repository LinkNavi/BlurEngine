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
#include "player.hpp"

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

// ===========================================================================
// SCENE SHADER  (skinning + three-point lighting + texture)
// ===========================================================================
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
out vec3 vWorldPos;

void main() {
    vec4 skinnedPos    = vec4(aPos, 1.0);
    vec3 skinnedNormal = aNormal;

    if (uIsSkinned) {
        mat4 skinMat =
            uBoneMatrices[aJoints.x] * aWeights.x +
            uBoneMatrices[aJoints.y] * aWeights.y +
            uBoneMatrices[aJoints.z] * aWeights.z +
            uBoneMatrices[aJoints.w] * aWeights.w;
        skinnedPos    = skinMat * vec4(aPos, 1.0);
        skinnedNormal = mat3(skinMat) * aNormal;
    }

    vec4 worldPos = uModel * skinnedPos;
    vWorldPos     = worldPos.xyz;
    vNormal       = mat3(uModel) * skinnedNormal;
    vUV           = aUV;
    gl_Position   = uProj * uView * worldPos;
}
)";

static const char* kFragSrc = R"(
#version 460 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
out vec4 FragColor;

uniform bool      uHasTexture;
uniform sampler2D uTexture;
uniform vec3      uLightDir;
uniform vec3      uLightColor;
uniform vec3      uFillDir;
uniform vec3      uFillColor;
uniform vec3      uAmbientColor;

void main() {
    vec3 n        = normalize(vNormal);
    float keyDiff = max(dot(n, uLightDir),  0.0);
    float fillDiff= max(dot(n, uFillDir),   0.0);
    vec3 light    = uAmbientColor + uLightColor * keyDiff + uFillColor * fillDiff;
    vec3 base     = uHasTexture ? texture(uTexture, vUV).rgb : vec3(0.8, 0.8, 0.85);
    FragColor     = vec4(base * light, 1.0);
}
)";

// ===========================================================================
// MOTION BLUR POST-PROCESS SHADER
// Renders a full-screen quad. Blends current frame with previous frame
// using a velocity-weighted alpha to fake directional speed blur.
// The strength is driven by a uniform so we can scale it with speed.
// ===========================================================================
static const char* kBlurVertSrc = R"(
#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV         = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kBlurFragSrc = R"(
#version 460 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uCurrent;  // scene rendered this frame
uniform sampler2D uPrevious; // scene rendered last frame
uniform float     uBlurStrength; // 0 = no blur, 1 = full ghosting

void main() {
    vec4 cur  = texture(uCurrent,  vUV);
    vec4 prev = texture(uPrevious, vUV);

    // Simple temporal blend: current dominates, ghost from previous
    FragColor = mix(cur, prev, uBlurStrength * 0.55);
}
)";

// ===========================================================================
// Camera tuning
// ===========================================================================
namespace {

struct CamTuning {
    // Offset behind and above the player. At low speed these are tighter;
    // at high speed the camera backs off so you can see what's coming.
    float baseDist    = 5.5f;   // distance behind at rest
    float maxDist     = 9.0f;   // max distance at top speed
    float baseHeight  = 2.2f;   // height above player at rest
    float maxHeight   = 3.5f;   // height at top speed
    float lookAtBias  = 1.4f;   // units ahead of player the camera looks at
    float yawEase     = 7.0f;   // how fast cam yaw follows player facing
    float fovBase     = 65.0f;  // FOV at rest
    float fovMax      = 95.0f;  // FOV at top speed (speed blur comes from this)
    float blurMax     = 0.45f;  // max motion blur strength (0..1)
};

constexpr CamTuning kCam;

float wrapAngle(float a) {
    while (a >  glm::pi<float>()) a -= glm::two_pi<float>();
    while (a < -glm::pi<float>()) a += glm::two_pi<float>();
    return a;
}

// Build a full-screen quad VAO for post-processing
unsigned int buildQuadVAO() {
    static const float verts[] = {
        // pos        uv
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 1.f,
    };
    static const unsigned int inds[] = { 0,1,2, 2,3,0 };

    unsigned int vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(inds), inds, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);
    return vao;
}

// Create / resize an RGBA8 FBO texture and attach it
unsigned int makeFramebuffer(int w, int h, unsigned int& outColorTex) {
    unsigned int fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &outColorTex);
    glBindTexture(GL_TEXTURE_2D, outColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outColorTex, 0);

    // Depth renderbuffer
    unsigned int rbo;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

} // namespace

// ===========================================================================
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
    std::string mapPath       = positional.size() > 1 ? positional[1] : "";
    bool debugMode            = debugFlag || mapPath.empty();

    const int W = 1280, H = 720;
    blur::Window   window(W, H, "Blur Engine");
    blur::Renderer renderer;
    blur::Shader   shader(kVertSrc, kFragSrc);
    blur::Shader   blurShader(kBlurVertSrc, kBlurFragSrc);
    blur::Gamepad  pad;

    // --- Bone uniform locations ---
    constexpr int kMaxBones = 128;
    std::vector<int> boneLocs(kMaxBones);
    for (int i = 0; i < kMaxBones; i++) {
        std::string name = "uBoneMatrices[" + std::to_string(i) + "]";
        boneLocs[i] = glGetUniformLocation(shader.id(), name.c_str());
    }

    // --- Lighting ---
    shader.use();
    shader.setVec3("uLightDir",    glm::normalize(glm::vec3( 0.5f, 1.0f,  0.3f)));
    shader.setVec3("uLightColor",  glm::vec3(0.88f, 0.85f, 0.82f));
    shader.setVec3("uFillDir",     glm::normalize(glm::vec3(-0.4f, 0.3f, -0.5f)));
    shader.setVec3("uFillColor",   glm::vec3(0.22f, 0.25f, 0.32f));
    shader.setVec3("uAmbientColor",glm::vec3(0.14f, 0.15f, 0.18f));

    // --- Character / world ---
    blur::Model charModel;
    bool charLoaded = !characterPath.empty() && blur::loadModel(characterPath, charModel);
    if (!characterPath.empty() && !charLoaded)
        std::fprintf(stderr, "Failed to load character: %s\n", characterPath.c_str());

    player::Player sonic(std::move(charModel));

    blur::Model          worldModel;
    blur::CollisionMesh  worldCollision;
    bool worldLoaded = false;
    if (!debugMode) {
        worldLoaded = blur::loadModel(mapPath, worldModel);
        if (worldLoaded) worldCollision.build(worldModel);
        else {
            std::fprintf(stderr, "Failed to load map: %s - debug mode\n", mapPath.c_str());
            debugMode = true;
        }
    }

    // --- Debug track ---
    debugmode::EndlessTrack track(6.0f, 2.0f, 40.0f, 1337u);
    blur::Texture noiseTexture;
    if (debugMode) {
        auto px = debugmode::generateNoisePixels(256, 256, 24, 7u);
        noiseTexture = blur::Texture::fromPixels(px.data(), 256, 256);
        track.update(0.0f, 120.0f);
    }
    float playerArc = 0.0f;

    // --- Post-process (motion blur) FBOs ---
    unsigned int curTex = 0, prevTex = 0;
    unsigned int curFBO  = makeFramebuffer(W, H, curTex);
    unsigned int prevFBO = makeFramebuffer(W, H, prevTex);
    unsigned int quadVAO = buildQuadVAO();

    // --- Camera state ---
    float camYaw    = 0.0f;
    float currentFov= kCam.fovBase;

    sonic.setPosition(glm::vec3(0.0f, 3.0f, 3.0f));

    double lastTime = glfwGetTime();

    while (!window.shouldClose()) {
        double now = glfwGetTime();
        float  dt  = glm::clamp((float)(now - lastTime), 0.0f, 1.0f/30.0f);
        lastTime   = now;

        window.pollEvents();
        SDL_PumpEvents();
        pad.update();

        // --- Input assembly ---
        player::InputState input;
        if (pad.isConnected()) {
            glm::vec2 ls = pad.leftStick();
            // stick.y forward = positive z wish (camera forward)
            // ls.y on gamepad is inverted (up = -1)
            input.stick      = glm::vec2(-ls.x, -ls.y);
            input.jump       = pad.wasPressed(blur::Button::A);
            input.jumpHeld   = pad.isPressed(blur::Button::A);
            input.drift      = pad.isPressed(blur::Button::B);
            input.quickLeft  = pad.wasPressed(blur::Button::LB);
            input.quickRight = pad.wasPressed(blur::Button::RB);
            input.slide      = pad.wasPressed(blur::Button::X);
            window.setWindowTitle((std::string("Blur Engine | ") + pad.name()).c_str());
        } else {
            window.setWindowTitle("Blur Engine | No gamepad");
        }

        // --- Track update ---
        if (debugMode) {
            track.update(playerArc, 120.0f);
            track.trim(playerArc,    60.0f);
        }

        // --- Camera basis from yaw ---
        glm::vec3 camDir = glm::normalize(glm::vec3(std::sin(camYaw), 0.0f, std::cos(camYaw)));
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 camRight = glm::normalize(glm::cross(worldUp, camDir));

        // --- Ground sample ---
        player::GroundSample ground;
        const glm::vec3& ppos = sonic.position();

        if (debugMode) {
            playerArc = track.closestArcLength(ppos, playerArc);
            debugmode::TrackFrame tf = track.sampleFrame(playerArc);

            // Project player onto track surface
            glm::vec3 toPos    = ppos - tf.position;
            float lateral      = glm::dot(toPos, tf.right);
            float halfWidth    = track.width() * 0.5f - 0.5f;
            lateral            = glm::clamp(lateral, -halfWidth, halfWidth);
            float fwdOnTrack   = glm::dot(toPos, tf.tangent);
            glm::vec3 surfPt   = tf.position + tf.right * lateral + tf.tangent * fwdOnTrack;

            ground.grounded = true;
            ground.point    = surfPt;
            ground.normal   = tf.up;
        } else if (worldLoaded && !worldCollision.empty()) {
            blur::CollisionHit hit;
            if (worldCollision.raycast(ppos + sonic.up() * 5.0f, -sonic.up(), 50.0f, hit)) {
                ground.grounded = true;
                ground.point    = hit.point;
                ground.normal   = hit.normal;
            }
        }

        // --- Player update ---
        sonic.update(dt, input, camDir, camRight, ground);
        std::printf("[MAIN] speed=%.2f pos=(%.2f,%.2f,%.2f) forward=(%.2f,%.2f,%.2f) state=%d\n",
            sonic.speed(),
            sonic.position().x, sonic.position().y, sonic.position().z,
            sonic.forward().x, sonic.forward().y, sonic.forward().z,
            (int)sonic.state());
        // --- Camera yaw tracking ---
        // Camera trails player facing. At high speed it lags a bit more so
        // Sonic can outrun the camera briefly (that Unleashed feeling of
        // going faster than the world can keep up with).
        float speedT   = glm::clamp(std::fabs(sonic.speed()) / 28.0f, 0.0f, 1.0f);
        float yawEase  = glm::mix(kCam.yawEase, kCam.yawEase * 0.5f, speedT);
        if (glm::length(input.stick) > 0.05f || std::fabs(sonic.speed()) > 0.5f) {
            float facingYaw = std::atan2(sonic.forward().x, sonic.forward().z);
            float diff      = wrapAngle(facingYaw - camYaw);
            camYaw         += diff * glm::clamp(yawEase * dt, 0.0f, 1.0f);
        }

        // --- Speed-driven camera distance + FOV ---
        float camDist   = glm::mix(kCam.baseDist,   kCam.maxDist,   speedT);
        float camHeight = glm::mix(kCam.baseHeight,  kCam.maxHeight, speedT);
        float targetFov = glm::mix(kCam.fovBase,     kCam.fovMax,    speedT * speedT);
        currentFov      = glm::mix(currentFov, targetFov, glm::clamp(5.0f * dt, 0.0f, 1.0f));

        camDir = glm::normalize(glm::vec3(std::sin(camYaw), 0.0f, std::cos(camYaw)));
        glm::vec3 camEye    = ppos - camDir * camDist + glm::vec3(0,1,0) * camHeight;
        glm::vec3 camTarget = ppos + sonic.up() * kCam.lookAtBias;
        glm::mat4 view      = glm::lookAt(camEye, camTarget, glm::vec3(0,1,0));
        glm::mat4 proj      = glm::perspective(
            glm::radians(currentFov),
            (float)window.width() / (float)window.height(),
            0.05f, 500.0f);

        // ==================================================================
        // RENDER PASS 1: scene → curFBO
        // ==================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, curFBO);
        glViewport(0, 0, W, H);
        renderer.clear();

        shader.use();
        shader.setMat4("uView", view);
        shader.setMat4("uProj", proj);

        const auto& bones    = sonic.animatedModel().boneMatrices();
        int boneCount        = (int)bones.size();
        for (int i = 0; i < boneCount && i < kMaxBones; i++)
            glUniformMatrix4fv(boneLocs[i], 1, GL_FALSE, glm::value_ptr(bones[i]));

        // Draw world / debug track
        if (debugMode) {
            shader.setMat4("uModel",      glm::mat4(1.0f));
            shader.setInt("uIsSkinned",   0);
            shader.setInt("uHasTexture",  1);
            noiseTexture.bind(0);
            shader.setInt("uTexture", 0);
            track.draw();
        } else if (worldLoaded) {
            const auto& wt = worldModel.textures;
            for (const auto& mesh : worldModel.meshes) {
                shader.setMat4("uModel",     mesh.transform);
                shader.setInt("uIsSkinned",  0);
                bool hasTex = mesh.textureIndex >= 0 && mesh.textureIndex < (int)wt.size();
                shader.setInt("uHasTexture", hasTex ? 1 : 0);
                if (hasTex) { wt[mesh.textureIndex].bind(0); shader.setInt("uTexture",0); }
                mesh.draw();
            }
        }

        // Draw player
        sonic.render(shader, boneLocs);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // ==================================================================
        // RENDER PASS 2: motion blur composite → backbuffer
        // ==================================================================
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, window.width(), window.height());
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Blur strength: quadratic in speed for a nice ramp
        float blurStrength = kCam.blurMax * speedT * speedT;

        blurShader.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, curTex);
        glUniform1i(glGetUniformLocation(blurShader.id(), "uCurrent"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, prevTex);
        glUniform1i(glGetUniformLocation(blurShader.id(), "uPrevious"), 1);

        glUniform1f(glGetUniformLocation(blurShader.id(), "uBlurStrength"), blurStrength);

        glBindVertexArray(quadVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);

        // Swap prev ← cur by swapping FBO handles (ping-pong)
        std::swap(curFBO, prevFBO);
        std::swap(curTex, prevTex);

        window.swapBuffers();
    }

    SDL_Quit();
    return 0;
}
