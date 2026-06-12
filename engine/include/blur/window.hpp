#pragma once

struct GLFWwindow;
#include<string>
namespace blur {

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();

    bool shouldClose() const;
    void pollEvents();
    void swapBuffers();
    void setWindowTitle(const char *title);
    int width() const { return m_width; }
    int height() const { return m_height; }
    GLFWwindow* handle() const { return m_window; }

private:
    GLFWwindow* m_window;
    int m_width;
    int m_height;
};

} // namespace blur
