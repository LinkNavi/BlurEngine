#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace blur {

// Xbox controller button layout
enum class Button {
    A, B, X, Y,
    LB, RB,
    Back, Start, Guide,
    LeftStick, RightStick,
    DpadUp, DpadRight, DpadDown, DpadLeft,
    Count
};

enum class Axis {
    LeftX, LeftY,
    RightX, RightY,
    LT, RT,
    Count
};

class Gamepad {
public:
    // index: which connected gamepad to bind (0 = first found). Call update()
    // every frame; it will (re)bind automatically if the device connects later
    // or gets unplugged/replugged.
    explicit Gamepad(int index = 0) : m_index(index) {}

    ~Gamepad() {
        if (m_handle) SDL_CloseGamepad(m_handle);
    }

    // Non-copyable (owns an SDL handle)
    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;

    bool isConnected() const { return m_handle != nullptr; }

    void update() {
        std::memcpy(m_prevButtons, m_buttons, sizeof(m_buttons));

        if (!m_handle) {
            tryConnect();
        }

        if (m_handle && !SDL_GamepadConnected(m_handle)) {
            SDL_CloseGamepad(m_handle);
            m_handle = nullptr;
        }

        if (!m_handle) {
            std::memset(m_buttons, 0, sizeof(m_buttons));
            std::memset(m_axes, 0, sizeof(m_axes));
            return;
        }

        static constexpr SDL_GamepadButton kButtonMap[(size_t)Button::Count] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST,  SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_GUIDE,
            SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT
        };

        for (size_t i = 0; i < (size_t)Button::Count; i++)
            m_buttons[i] = SDL_GetGamepadButton(m_handle, kButtonMap[i]);

        static constexpr SDL_GamepadAxis kAxisMap[(size_t)Axis::Count] = {
            SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY,
            SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
            SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
        };

        for (size_t i = 0; i < (size_t)Axis::Count; i++) {
            // SDL axis range is -32768..32767 (triggers are 0..32767)
            float raw = SDL_GetGamepadAxis(m_handle, kAxisMap[i]) / 32767.0f;

            if (i == (size_t)Axis::LT || i == (size_t)Axis::RT) {
                m_axes[i] = raw < m_triggerDeadzone ? 0.0f : raw;
            } else {
                m_axes[i] = std::fabs(raw) < m_stickDeadzone ? 0.0f : raw;
            }
        }
    }

    // Held this frame
    bool isPressed(Button b) const { return m_buttons[(size_t)b]; }

    // True only on the frame the button went down
    bool wasPressed(Button b) const {
        size_t i = (size_t)b;
        return m_buttons[i] && !m_prevButtons[i];
    }

    // True only on the frame the button went up
    bool wasReleased(Button b) const {
        size_t i = (size_t)b;
        return !m_buttons[i] && m_prevButtons[i];
    }

    // Currently up
    bool isReleased(Button b) const { return !m_buttons[(size_t)b]; }

    float getAxis(Axis a) const { return m_axes[(size_t)a]; }

    glm::vec2 leftStick() const {
        glm::vec2 v(m_axes[(size_t)Axis::LeftX], m_axes[(size_t)Axis::LeftY]);
        float len = glm::length(v);
        return len > 1.0f ? v / len : v;
    }

    glm::vec2 rightStick() const {
        glm::vec2 v(m_axes[(size_t)Axis::RightX], m_axes[(size_t)Axis::RightY]);
        float len = glm::length(v);
        return len > 1.0f ? v / len : v;
    }

    void setStickDeadzone(float dz) { m_stickDeadzone = dz; }
    void setTriggerDeadzone(float dz) { m_triggerDeadzone = dz; }

    const char* name() const {
        return m_handle ? SDL_GetGamepadName(m_handle) : "disconnected";
    }

private:
    void tryConnect() {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids) return;

        if (m_index < count) {
            SDL_Gamepad* h = SDL_OpenGamepad(ids[m_index]);
            if (h) {
                m_handle = h;
                std::printf("Gamepad connected: %s\n", SDL_GetGamepadName(h));
            }
        }
        SDL_free(ids);
    }

    int m_index;
    SDL_Gamepad* m_handle = nullptr;

    bool m_buttons[(size_t)Button::Count] = {};
    bool m_prevButtons[(size_t)Button::Count] = {};
    float m_axes[(size_t)Axis::Count] = {};

    float m_stickDeadzone = 0.2f;
    float m_triggerDeadzone = 0.05f;
};

} // namespace blur
