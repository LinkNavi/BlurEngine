#pragma once

#include <glm/glm.hpp>

namespace blur {

class Renderer {
public:
    Renderer();
    ~Renderer();

    void clear(const glm::vec4& color = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));
};

} // namespace blur
