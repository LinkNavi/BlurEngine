#pragma once

#include <vector>

namespace debugmode {

// Tileable 2D value noise baked to an RGBA8 buffer - cheap surface-detail
// texture so flat debug geometry doesn't read as one flat color.
std::vector<unsigned char> generateNoisePixels(int width, int height, int cellSize, unsigned int seed);

} // namespace debugmode
