#include "blur/window.hpp"
#include "blur/renderer.hpp"
#include "blur/shader.hpp"
#include "blur/mesh.hpp"
#include "blur/gamepad.hpp"

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <cstdio>

// Skinned vertex shader: blends position/normal by up to 4 joint weights.
// uBoneMatrices is sized generously; unused entries stay identity and are
// simply unreferenced for meshes with fewer bones or no skin at all (in
// which case every vertex is weight=1.0 on joint 0, which we keep as identity).
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
    gl_Position = uProj * uView * uModel * skinnedPos;
}
)";

static const char* kFragSrc = R"(
#version 460 core
in vec3 vNormal;
out vec4 FragColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(normalize(vNormal), lightDir), 0.0);
    vec3 color = vec3(0.8, 0.8, 0.85) * (0.3 + 0.7 * diff);
    FragColor = vec4(color, 1.0);
}
)";

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    blur::Window window(1280, 720, "Blur Engine");
    blur::Renderer renderer;
    blur::Shader shader(kVertSrc, kFragSrc);
    blur::Gamepad pad;

    blur::Model model;
    bool loaded = false;
    if (argc > 1) {
        loaded = blur::loadModel(argv[1], model);
    }

    blur::AnimatedModel animModel(std::move(model));

    if (loaded && !animModel.hasSkeleton()) {
        std::printf("Loaded model has no skeleton (static mesh)\n");
    } else if (loaded) {
        std::printf("Loaded model with skeleton\n");
        // Auto-play the first animation if one exists, just so something
        // visibly moves once Sonic's clips are in.
        animModel.playAnimation(0, /*loop=*/true);
    }

    // Toggle this (or bind it to a gamepad button below) to see the wobble effect.
    bool wobbleOn = false;
    animModel.setWobble(wobbleOn, /*amplitude=*/0.08f, /*frequency=*/5.0f);

    glm::vec3 playerPos(0.0f);
    float playerSpeed = 5.0f;

    double lastTime = glfwGetTime();

    while (!window.shouldClose()) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;

        window.pollEvents();
        SDL_PumpEvents();
        pad.update();

        if (!pad.isConnected()) {
            window.setWindowTitle("Blur Engine - No gamepad detected");
        } else {
            glm::vec2 stick = pad.leftStick();
            playerPos.x += stick.x * playerSpeed * dt;
            playerPos.z += stick.y * playerSpeed * dt;

            if (pad.wasPressed(blur::Button::A)) {
                std::printf("A pressed (jump)\n");
            }
            if (pad.wasPressed(blur::Button::Y)) {
                // Toggle wobble for testing - "lil dude wobbly" button.
                wobbleOn = !wobbleOn;
                animModel.setWobble(wobbleOn, 0.08f, 5.0f);
                std::printf("Wobble %s\n", wobbleOn ? "ON" : "OFF");
            }
            if (pad.wasPressed(blur::Button::Start)) {
                std::printf("Start pressed (pause menu)\n");
            }

            window.setWindowTitle((std::string("Blur Engine - ") + pad.name()).c_str());
        }

        animModel.update(dt);

        renderer.clear();

        glm::mat4 view = glm::lookAt(glm::vec3(0, 1.5f, 4.0f), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(60.0f),
            (float)window.width() / (float)window.height(), 0.1f, 1000.0f);

        shader.use();
        shader.setMat4("uView", view);
        shader.setMat4("uProj", proj);

        // Upload bone matrices (identity-filled if unskinned).
        const auto& bones = animModel.boneMatrices();
        int boneCount = (int)bones.size();
        glUniform1i(glGetUniformLocation(shader.id(), "uIsSkinned"), boneCount > 0 ? 1 : 0);
        for (int i = 0; i < boneCount; i++) {
            std::string name = "uBoneMatrices[" + std::to_string(i) + "]";
            shader.setMat4(name.c_str(), bones[i]);
        }

        glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), playerPos);
        shader.setMat4("uModel", modelMat);
        animModel.draw();

        window.swapBuffers();
    }

    SDL_Quit();
    return 0;
}
