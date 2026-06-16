#pragma once

#include "blur/mesh.hpp"
#include "blur/shader.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace player {

// Result of asking "what's the ground like right here" for a given world
// position. Filled in by whatever the host app is using for collision
// (a real CollisionMesh raycast, the debug EndlessTrack, future trigger
// volumes, whatever) - the Player itself never knows or cares which.
struct GroundSample {
    bool grounded = false;
    glm::vec3 point = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0, 1, 0);
};

// Car-like steering + SA2/Unleashed-style "gravity follows the surface"
// ground movement. Tuning lives here so any map can drop a Player in and
// get the same feel; nothing in here is aware of any specific track/map.
struct PlayerTuning {
    float lookAheadDist    = 4.0f;   // how far ahead the wish point sits
    float maxSpeed          = 16.0f;  // top ground speed
    float accel              = 22.0f;  // speed gain per second when holding a direction
    float decel              = 14.0f;  // speed loss per second when stick is neutral
    float brakeDecel         = 36.0f;  // speed loss per second when steering hard against current motion
    float minTurnRate        = 5.5f;   // rad/s turn rate at zero speed (sharp pivot)
    float maxTurnRate        = 1.1f;   // rad/s turn rate at max speed (wide arc)
    float slopeAccelScale    = 6.0f;   // extra accel per second from downhill grade

    // SA2/Unleashed-style ground stick: the surface normal directly under
    // Sonic's feet is sampled every frame (not predicted ahead), and gravity
    // tilts to match it so loops/banked turns/walls all just work. We only
    // reject a surface as "too steep to be ground" (i.e. treat it as a wall)
    // when the normal changes too sharply *between consecutive frames* -
    // a smoothly curving wall you're already running along never trips this,
    // only a genuinely abrupt change does. Faster movement tolerates a
    // bigger per-frame change, since covering more arc length per frame is
    // expected to read as a bigger normal delta even on a smooth curve.
    float wallDeltaBase      = 0.62f;  // rad max per-frame normal change tolerated at zero speed
    float wallDeltaPerSpeed  = 0.022f; // extra rad of tolerance per unit of speed
    float wallDeltaMax       = 1.25f;  // rad absolute cap regardless of speed
    float wallImpactDecel    = 0.85f;  // fraction of speed kept after a hard wall hit

    float upEase             = 10.0f;  // how fast currentUp eases toward target up (normal cases)
    float groundProbeUp      = 5.0f;   // how far above the player to start a downward ground probe
    float groundProbeDown    = 50.0f;  // how far below to search for ground
};

class Player {
public:
    explicit Player(blur::Model model, PlayerTuning tuning = {});

    // stickDir: camera-relative wish direction, already sign-corrected
    // (forward-tilt should map to +stickDir along camForward). Magnitude
    // up to 1 conveys how far the stick is pushed.
    // ground: result of the host's ground query for the player's current
    // (pre-move) position; pass grounded=false for airborne/no-map cases.
    // camForward/camRight: yaw-only camera basis, used to build the wish
    // point the same way regardless of who's driving the camera.
    void update(float dt, const glm::vec2& stickDir, const glm::vec3& camForward,
                const glm::vec3& camRight, const GroundSample& ground);

    void playAnimation(const std::string& name, bool loop = true);
    void playAnimation(int index, bool loop = true);

    void render(class blur::Shader& shader, const std::vector<int>& boneLocs) const;

    const glm::vec3& position() const { return m_position; }
    const glm::vec3& forward() const { return m_forward; }
    const glm::vec3& right() const { return m_right; }
    const glm::vec3& up() const { return m_up; }
    float speed() const { return m_speed; }
    bool justHitWall() const { return m_hitWallThisFrame; }

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    void setUp(const glm::vec3& up) { m_up = glm::normalize(up); }

    glm::mat4 basis() const;
    glm::mat4 modelMatrix() const;

    blur::AnimatedModel& animatedModel() { return m_animModel; }
    const blur::AnimatedModel& animatedModel() const { return m_animModel; }

private:
    PlayerTuning m_tuning;
    blur::AnimatedModel m_animModel;

    glm::vec3 m_position{0.0f};
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    float m_speed = 0.0f;

    bool m_hasPrevGroundNormal = false;
    glm::vec3 m_prevGroundNormal{0.0f, 1.0f, 0.0f};
    bool m_hitWallThisFrame = false;
};

} // namespace player
