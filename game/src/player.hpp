#pragma once

#include "blur/mesh.hpp"
#include "blur/shader.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace player {

// GroundSample: result of asking "what's the ground like right here"
struct GroundSample {
    bool grounded = false;
    glm::vec3 point  = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0, 1, 0);
};

// Button state snapshot passed in from main each frame so Player stays
// completely input-agnostic.
struct InputState {
    glm::vec2 stick       = glm::vec2(0.0f); // camera-relative, magnitude 0..1
    bool      jump        = false;            // wasPressed this frame
    bool      jumpHeld    = false;            // isPressed this frame
    bool      drift       = false;            // isPressed (hold to drift)
    bool      quickLeft   = false;            // wasPressed
    bool      quickRight  = false;            // wasPressed
    bool      slide       = false;            // wasPressed (stomp / slide on ground)
};

struct PlayerTuning {
    // --- Ground movement ---
    float maxSpeed          = 28.0f;   // top run speed (Unleashed is FAST)
    float startSpeed        = 8.0f;    // set at stage start, gained through momentum
    float accel             = 3.0f;   // gain per second while pushing stick
    float decel             = 10.0f;   // loss per second when stick neutral
    float brakeDecel        = 48.0f;   // loss per second when pushing against heading
    float turnRateSlow = 2.0f;   // rad/s when nearly stopped (tight, responsive)
    float turnRateFast = 0.8f;   // rad/s at max speed (wide arc)
    float lookAheadDist     = 5.0f;

    // --- Slope momentum ---
    float slopeAccel        = 12.0f;   // speed gain per unit of downhill grade
    float slopeDecel        = 8.0f;    // loss per unit of uphill grade

    // --- Drift ---
    // Drift tightens or loosens based on speed. The faster you go the bigger
    // the arc (harder to steer out), but you carry speed through the turn.
    float driftTurnRateSlow = 5.0f;    // rad/s at low speed during drift
    float driftTurnRateFast = 2.8f;    // rad/s at high speed during drift
    float driftSpeedRetain  = 0.96f;   // fraction of speed kept per second while drifting

    // --- Quickstep ---
    float quickstepDist     = 1.8f;    // lateral offset
    float quickstepTime     = 0.12f;   // how long it takes

    // --- Jump / airborne ---
    float jumpPower         = 8.5f;    // initial vertical impulse
    float gravity           = 22.0f;   // downward accel while airborne
    float airControlScale   = 0.35f;   // how much the stick steers in the air
    float airTurnRate       = 1.8f;    // rad/s steering rate while airborne

    // --- Surface tracking ---
    float wallDeltaBase     = 0.62f;
    float wallDeltaPerSpeed = 0.022f;
    float wallDeltaMax      = 1.25f;
    float wallImpactDecel   = 0.80f;
    float upEase            = 12.0f;
    float groundProbeUp     = 5.0f;
    float groundProbeDown   = 50.0f;
};

// Movement state machine
enum class MoveState { Ground, Airborne, Drift, Quickstep };

class Player {
public:
    explicit Player(blur::Model model, PlayerTuning tuning = {});

    // Called every frame. Returns nothing; query state via accessors.
    void update(float dt, const InputState& input,
                const glm::vec3& camForward, const glm::vec3& camRight,
                const GroundSample& ground);

    void render(blur::Shader& shader, const std::vector<int>& boneLocs) const;

    void playAnimation(const std::string& name, bool loop = true);
    void playAnimation(int index, bool loop = true);

    // --- Accessors ---
    const glm::vec3& position()  const { return m_pos; }
    const glm::vec3& forward()   const { return m_forward; }
    const glm::vec3& right()     const { return m_right; }
    const glm::vec3& up()        const { return m_up; }
    float speed()                const { return m_speed; }
    MoveState state()            const { return m_state; }
    bool isGrounded()            const { return m_state != MoveState::Airborne; }
    bool justHitWall()           const { return m_hitWall; }

    void setPosition(const glm::vec3& p)  { m_pos = p; }
    void setUp(const glm::vec3& u)        { m_up = glm::normalize(u); }
    void setSpeed(float s)                { m_speed = s; }

    glm::mat4 basis()       const;
    glm::mat4 modelMatrix() const;

    blur::AnimatedModel&       animatedModel()       { return m_anim; }
    const blur::AnimatedModel& animatedModel() const { return m_anim; }

private:
    void updateGround(float dt, const InputState& in,
                      const glm::vec3& camForward, const glm::vec3& camRight,
                      const GroundSample& ground);
    void updateDrift(float dt, const InputState& in,
                     const glm::vec3& camForward, const glm::vec3& camRight,
                     const GroundSample& ground);
    void updateAirborne(float dt, const InputState& in,
                        const glm::vec3& camForward, const glm::vec3& camRight,
                        const GroundSample& ground);
    void updateQuickstep(float dt, const GroundSample& ground);

    void resolveGround(const GroundSample& g, glm::vec3& nextPos, glm::vec3& newUp);
    void reorthogonalize();

    PlayerTuning      m_tuning;
    blur::AnimatedModel m_anim;

    glm::vec3  m_pos     {0.0f};
    glm::vec3  m_forward {0.0f, 0.0f, 1.0f};
    glm::vec3  m_right   {1.0f, 0.0f, 0.0f};
    glm::vec3  m_up      {0.0f, 1.0f, 0.0f};

    float      m_speed   = 0.0f;
    glm::vec3  m_vertVel = glm::vec3(0.0f); // only used airborne (vertical component)

    MoveState  m_state   = MoveState::Ground;
    bool       m_hitWall = false;

    bool       m_hasPrevNormal = false;
    glm::vec3  m_prevNormal    {0.0f, 1.0f, 0.0f};

    // Quickstep
    float      m_qsTimer    = 0.0f;
    glm::vec3  m_qsTarget   {0.0f};

    // For animation selection
    std::string m_curAnim;
};

} // namespace player
