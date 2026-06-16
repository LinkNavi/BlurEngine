#include "noise.hpp"

#include <cmath>

namespace debugmode {

namespace {

unsigned int hash(unsigned int x, unsigned int y, unsigned int seed) {
    unsigned int h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float randAt(int cellX, int cellY, unsigned int seed) {
    return (hash((unsigned int)cellX, (unsigned int)cellY, seed) & 0xFFFFu) / 65535.0f;
}

float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

float valueNoise(float x, float y, unsigned int seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    float tx = smoothstep(x - x0);
    float ty = smoothstep(y - y0);

    float v00 = randAt(x0, y0, seed);
    float v10 = randAt(x1, y0, seed);
    float v01 = randAt(x0, y1, seed);
    float v11 = randAt(x1, y1, seed);

    float a = v00 + (v10 - v00) * tx;
    float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * ty;
}

} // namespace

std::vector<unsigned char> generateNoisePixels(int width, int height, int cellSize, unsigned int seed) {
    std::vector<unsigned char> pixels((size_t)width * height * 4);
    float invCell = 1.0f / (float)cellSize;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float n1 = valueNoise(x * invCell, y * invCell, seed);
            float n2 = valueNoise(x * invCell * 3.3f, y * invCell * 3.3f, seed + 1);
            float n = n1 * 0.7f + n2 * 0.3f;

            unsigned char v = (unsigned char)(180 + n * 60.0f);

            size_t idx = ((size_t)y * width + x) * 4;
            pixels[idx + 0] = v;
            pixels[idx + 1] = v;
            pixels[idx + 2] = v;
            pixels[idx + 3] = 255;
        }
    }

    return pixels;
}

} // namespace debugmode
