#include "blur/window.hpp"
#include "blur/renderer.hpp"
#include "blur/shader.hpp"
#include "blur/mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

static const char* kVertSrc = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormal;

void main() {
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
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
    blur::Window window(1280, 720, "Blur Engine");
    blur::Renderer renderer;
    blur::Shader shader(kVertSrc, kFragSrc);

    std::vector<blur::Mesh> meshes;
    if (argc > 1) {
        blur::loadGltf(argv[1], meshes);
    }

    glm::mat4 view = glm::lookAt(glm::vec3(0, 1.5f, 4.0f), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f),
        (float)window.width() / (float)window.height(), 0.1f, 1000.0f);

    while (!window.shouldClose()) {
        window.pollEvents();

        renderer.clear();

        shader.use();
        shader.setMat4("uView", view);
        shader.setMat4("uProj", proj);

        for (auto& mesh : meshes) {
            shader.setMat4("uModel", mesh.transform);
            mesh.draw();
        }

        window.swapBuffers();
    }

    return 0;
}
