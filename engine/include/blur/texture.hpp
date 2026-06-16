#pragma once

namespace blur {

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // pixels must be RGBA8, width*height*4 bytes.
    static Texture fromPixels(const unsigned char* pixels, int width, int height);

    void bind(unsigned int unit = 0) const;

    unsigned int id() const { return m_id; }
    bool valid() const { return m_id != 0; }

private:
    unsigned int m_id = 0;
};

} // namespace blur
