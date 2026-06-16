#define GLM_ENABLE_EXPERIMENTAL

#include "player.hpp"
#include "blur/shader.hpp"
#include <cstdio>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>

namespace player {

namespace {
float wrapAngle(float a) {
    while (a >  glm::pi<float>()) a -= glm::two_pi<float>();
    while (a < -glm::pi<float>()) a += glm::two_pi<float>();
    return a;
}

// Rotate `v` around `axis` by `angle` radians.
glm::vec3 rotateAround(const glm::vec3& v, const glm::vec3& axis, float angle) {
    glm::quat q = glm::angleAxis(angle, axis);
    return q * v;
}
} // namespace

// ---------------------------------------------------------------------------
Player::Player(blur::Model model, PlayerTuning tuning)
    : m_tuning(tuning), m_anim(std::move(model)) {
    m_speed = m_tuning.startSpeed;
    if (!m_anim.animations().empty())
        m_anim.playAnimation(0, true);
}

void Player::playAnimation(const std::string& name, bool loop) { m_anim.playAnimation(name, loop); }
void Player::playAnimation(int index,                bool loop) { m_anim.playAnimation(index, loop); }

// ---------------------------------------------------------------------------
void Player::update(float dt, const InputState& input,
                    const glm::vec3& camForward, const glm::vec3& camRight,
                    const GroundSample& ground) {
    m_hitWall = false;

    switch (m_state) {
        case MoveState::Ground:
            // Jump takes priority
            if (input.jump && ground.grounded) {
                m_state   = MoveState::Airborne;
                m_vertVel = m_up * m_tuning.jumpPower;
                // carry full horizontal momentum into air
                updateAirborne(dt, input, camForward, camRight, ground);
                break;
            }
            // Enter drift
            if (input.drift && std::fabs(m_speed) > 2.0f) {
                m_state = MoveState::Drift;
                updateDrift(dt, input, camForward, camRight, ground);
                break;
            }
            // Quickstep
            if ((input.quickLeft || input.quickRight) && m_qsTimer <= 0.0f) {
                m_state    = MoveState::Quickstep;
                float side = input.quickLeft ? -1.0f : 1.0f;
                m_qsTarget = m_pos + m_right * (m_tuning.quickstepDist * side);
                m_qsTimer  = m_tuning.quickstepTime;
            }
            if (m_state == MoveState::Quickstep) {
                updateQuickstep(dt, ground);
                break;
            }
            updateGround(dt, input, camForward, camRight, ground);
            break;

        case MoveState::Drift:
            if (!input.drift || std::fabs(m_speed) < 0.5f) {
                m_state = MoveState::Ground;
                updateGround(dt, input, camForward, camRight, ground);
            } else {
                updateDrift(dt, input, camForward, camRight, ground);
            }
            break;

        case MoveState::Airborne:
            updateAirborne(dt, input, camForward, camRight, ground);
            break;

        case MoveState::Quickstep:
            updateQuickstep(dt, ground);
            if (m_qsTimer <= 0.0f) {
                m_state = MoveState::Ground;
                updateGround(dt, input, camForward, camRight, ground);
            }
            break;
    }

    m_anim.update(dt);
}

// ---------------------------------------------------------------------------
// Ground movement: wish-point steering + slope momentum
// ---------------------------------------------------------------------------
void Player::updateGround(float dt, const InputState& in,
                           const glm::vec3& camForward, const glm::vec3& camRight,
                           const GroundSample& ground) {
    static int frame = 0;
    frame++;

    float stickMag = glm::length(in.stick);
    std::printf("[F%04d-G] updateGround | stick=(%.3f,%.3f) mag=%.3f speed=%.2f pos=(%.2f,%.2f,%.2f)\n",
        frame, in.stick.x, in.stick.y, stickMag, m_speed, m_pos.x, m_pos.y, m_pos.z);

    // --- Wish-point steering ---
    glm::vec3 stickWorld = camForward * in.stick.y + camRight * in.stick.x;
    glm::vec3 wishPos = m_pos + stickWorld * m_tuning.lookAheadDist;
    glm::vec3 toWish = wishPos - m_pos;
    toWish -= m_up * glm::dot(toWish, m_up);

    if (stickMag > 1e-3f && glm::length2(toWish) > 1e-8f) {
          glm::vec3 toWishN = glm::normalize(toWish);
          glm::vec3 fwdFlat = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));

          float tYaw = std::atan2(toWishN.x, toWishN.z);
          float cYaw = std::atan2(fwdFlat.x, fwdFlat.z);
          float diff = wrapAngle(tYaw - cYaw);

          float speedT = glm::clamp(std::fabs(m_speed) / m_tuning.maxSpeed, 0.0f, 1.0f);

          // Non-ground steering: fast = wide arcs, forces drift decision
          // At low speed: responsive, can turn tight
          // At high speed: sluggish, drift required for tight turns
          float responsiveness = std::exp(-2.5f * speedT);
          float turnRate = glm::mix(m_tuning.turnRateFast, m_tuning.turnRateSlow, responsiveness);

          // Clamp diff so you can't instantly snap around at high speed
          float maxDiff = glm::mix(glm::pi<float>() * 0.9f, glm::pi<float>() * 0.25f, speedT);
          diff = glm::clamp(diff, -maxDiff, maxDiff);

          float step = glm::clamp(diff, -turnRate * dt, turnRate * dt);
          m_forward = rotateAround(m_forward, m_up, step);
      }

    // --- Accel / decel ---
    float headingDot = 0.0f;
    if (stickMag > 1e-3f) {
        glm::vec3 fwdFlat = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));
        glm::vec3 wishFlat = glm::normalize(stickWorld - m_up * glm::dot(stickWorld, m_up));
        headingDot = glm::dot(fwdFlat, wishFlat);
    }

    float oldSpeed = m_speed;
    if (stickMag > 0.05f) {
        if (headingDot < -0.25f && m_speed > 1.0f) {
            m_speed -= m_tuning.brakeDecel * dt;
            std::printf("[F%04d-G]   BRAKING | headingDot=%.3f speed %.2f -> %.2f\n",
                frame, headingDot, oldSpeed, m_speed);
        } else {
            m_speed += m_tuning.accel * stickMag * dt;
            std::printf("[F%04d-G]   ACCEL | headingDot=%.3f speed %.2f -> %.2f\n",
                frame, headingDot, oldSpeed, m_speed);
        }
    } else {
        float d = m_tuning.decel * dt;
        m_speed = (m_speed > 0.0f) ? std::max(0.0f, m_speed - d)
                                    : std::min(0.0f, m_speed + d);
        std::printf("[F%04d-G]   COAST | speed %.2f -> %.2f\n", frame, oldSpeed, m_speed);
    }

    // --- Slope momentum ---
    float downDot = glm::dot(glm::normalize(m_forward), glm::vec3(0, -1, 0));
    if (downDot > 0.0f)
        m_speed += downDot * m_tuning.slopeAccel * dt;
    else
        m_speed += downDot * m_tuning.slopeDecel * dt;

    m_speed = glm::clamp(m_speed, -m_tuning.maxSpeed * 0.4f, m_tuning.maxSpeed);

    glm::vec3 velocity = m_forward * m_speed;
    glm::vec3 nextPos = m_pos + velocity * dt;
    glm::vec3 newUp = m_up;

    std::printf("[F%04d-G]   pre-resolve | forward=(%.2f,%.2f,%.2f) velocity=(%.2f,%.2f,%.2f) nextPos=(%.2f,%.2f,%.2f)\n",
        frame, m_forward.x, m_forward.y, m_forward.z,
        velocity.x, velocity.y, velocity.z,
        nextPos.x, nextPos.y, nextPos.z);

    resolveGround(ground, nextPos, newUp);

    glm::vec3 posBefore = m_pos;
    m_pos = nextPos;
    float easeRate = m_hitWall ? 1e6f : m_tuning.upEase;
    m_up = glm::normalize(glm::mix(m_up, newUp, glm::clamp(easeRate * dt, 0.0f, 1.0f)));
    reorthogonalize();

    std::printf("[F%04d-G]   post-resolve | pos (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f) delta=(%.4f,%.4f,%.4f) hitWall=%d\n",
        frame, posBefore.x, posBefore.y, posBefore.z,
        m_pos.x, m_pos.y, m_pos.z,
        m_pos.x - posBefore.x, m_pos.y - posBefore.y, m_pos.z - posBefore.z,
        m_hitWall);
}

// ---------------------------------------------------------------------------
// Drift: higher arc at high speed, retain speed through turns
// ---------------------------------------------------------------------------
void Player::updateDrift(float dt, const InputState& in,
                          const glm::vec3& camForward, const glm::vec3& camRight,
                          const GroundSample& ground) {
    float stickMag = glm::length(in.stick);
    glm::vec3 stickWorld = camForward * in.stick.y + camRight * in.stick.x;

    if (stickMag > 1e-3f) {
        glm::vec3 toWish = stickWorld * m_tuning.lookAheadDist;
        toWish -= m_up * glm::dot(toWish, m_up);

        if (glm::length2(toWish) > 1e-8f) {
            glm::vec3 toWishN = glm::normalize(toWish);
            glm::vec3 fwdFlat = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));

            float tYaw = std::atan2(toWishN.x, toWishN.z);
            float cYaw = std::atan2(fwdFlat.x,  fwdFlat.z);
            float diff = wrapAngle(tYaw - cYaw);

            // Speed-dependent drift arc: fast = slower turn (bigger swing)
            float speedT   = glm::clamp(std::fabs(m_speed) / m_tuning.maxSpeed, 0.0f, 1.0f);
            float turnRate = glm::mix(m_tuning.driftTurnRateSlow, m_tuning.driftTurnRateFast, speedT);
            float step     = glm::clamp(diff, -turnRate * dt, turnRate * dt);

            m_forward = rotateAround(m_forward, m_up, step);
        }
    }

    // Speed bleeds slightly during drift, but you keep most of it
    m_speed *= std::pow(m_tuning.driftSpeedRetain, dt);
    m_speed  = glm::clamp(m_speed, 0.0f, m_tuning.maxSpeed);

    // Slope still applies in drift
    float downDot = glm::dot(glm::normalize(m_forward), glm::vec3(0, -1, 0));
    m_speed += downDot * (downDot > 0.0f ? m_tuning.slopeAccel : m_tuning.slopeDecel) * dt;
    m_speed  = glm::clamp(m_speed, 0.0f, m_tuning.maxSpeed);

    glm::vec3 nextPos = m_pos + m_forward * m_speed * dt;
    glm::vec3 newUp   = m_up;
    resolveGround(ground, nextPos, newUp);

    m_pos = nextPos;
    m_up  = glm::normalize(glm::mix(m_up, newUp, glm::clamp(m_tuning.upEase * dt, 0.0f, 1.0f)));
    reorthogonalize();
}

// ---------------------------------------------------------------------------
// Airborne: gravity, full horizontal momentum carry, air steering
// ---------------------------------------------------------------------------
void Player::updateAirborne(float dt, const InputState& in,
                             const glm::vec3& camForward, const glm::vec3& camRight,
                             const GroundSample& ground) {
    // Gravity along world down (not surface up - we're in the air)
    m_vertVel += glm::vec3(0, -1, 0) * m_tuning.gravity * dt;

    // Air steering: gentle nudge to heading, carries horizontal speed
    float stickMag = glm::length(in.stick);
    if (stickMag > 1e-3f) {
        glm::vec3 stickWorld = camForward * in.stick.y + camRight * in.stick.x;
        glm::vec3 toWish = stickWorld - glm::vec3(0,1,0) * glm::dot(stickWorld, glm::vec3(0,1,0));
        if (glm::length2(toWish) > 1e-8f) {
            glm::vec3 toWishN = glm::normalize(toWish);
            glm::vec3 fwdFlat = glm::normalize(m_forward - glm::vec3(0,1,0) * m_forward.y);

            float tYaw = std::atan2(toWishN.x, toWishN.z);
            float cYaw = std::atan2(fwdFlat.x,  fwdFlat.z);
            float diff = wrapAngle(tYaw - cYaw);
            float step = glm::clamp(diff, -m_tuning.airTurnRate * dt, m_tuning.airTurnRate * dt);

            m_forward = rotateAround(m_forward, glm::vec3(0,1,0), step);
        }
    }

    // Move: horizontal from m_speed*forward + vertical from vertVel
    glm::vec3 hvel    = m_forward * m_speed;
    glm::vec3 nextPos = m_pos + (hvel + m_vertVel) * dt;

    // Landing check
    if (ground.grounded && m_vertVel.y <= 0.0f) {
        // Check we're close enough to the ground
        float dist = glm::distance(nextPos, ground.point);
        if (dist < 1.5f || nextPos.y <= ground.point.y) {
            nextPos   = ground.point;
            m_vertVel = glm::vec3(0.0f);
            m_up      = ground.normal;
            m_hasPrevNormal = true;
            m_prevNormal    = ground.normal;
            m_state   = MoveState::Ground;
            reorthogonalize();
            m_pos = nextPos;
            return;
        }
    }

    m_pos = nextPos;
    // Keep up roughly world-up while airborne
    m_up = glm::normalize(glm::mix(m_up, glm::vec3(0,1,0), glm::clamp(3.0f * dt, 0.0f, 1.0f)));
    reorthogonalize();
}

// ---------------------------------------------------------------------------
// Quickstep: lerp sideways over quickstepTime
// ---------------------------------------------------------------------------
void Player::updateQuickstep(float dt, const GroundSample& ground) {
    m_qsTimer -= dt;
    float alpha  = 1.0f - glm::clamp(m_qsTimer / m_tuning.quickstepTime, 0.0f, 1.0f);
    glm::vec3 np = glm::mix(m_pos, m_qsTarget, glm::clamp(alpha * 2.0f, 0.0f, 1.0f));

    // Still move forward
    np += m_forward * m_speed * dt;

    glm::vec3 newUp = m_up;
    resolveGround(ground, np, newUp);
    m_pos = np;
    m_up  = glm::normalize(glm::mix(m_up, newUp, glm::clamp(m_tuning.upEase * dt, 0.0f, 1.0f)));
    reorthogonalize();
}

// ---------------------------------------------------------------------------
// Ground stick: track the surface, detect walls, snap position
// ---------------------------------------------------------------------------
void Player::resolveGround(const GroundSample& g, glm::vec3& nextPos, glm::vec3& newUp) {
    if (!g.grounded) {
        m_hasPrevNormal = false;
        return;
    }

    if (m_hasPrevNormal) {
        float deltaAngle = std::acos(glm::clamp(glm::dot(m_prevNormal, g.normal), -1.0f, 1.0f));
        float threshold = glm::min(
            m_tuning.wallDeltaBase + m_tuning.wallDeltaPerSpeed * std::fabs(m_speed),
            m_tuning.wallDeltaMax);

        if (deltaAngle <= threshold) {
            newUp = g.normal;

            // How far above the ground plane are we?
            float heightAbove = glm::dot(nextPos - g.point, g.normal);

            // If heightAbove is near zero, the ground sample is at our old pos.
            // That means we're moving horizontally - project onto plane, don't snap to point.
            if (heightAbove > 0.1f) {
                // We're genuinely above the ground, project down onto it
                nextPos = nextPos - g.normal * heightAbove;
            } else if (heightAbove < -0.5f) {
                // We're below ground, snap up
                nextPos = g.point;
            }
            // else: heightAbove is ~0, we're on the plane already.
            // nextPos stays as-is (preserves horizontal movement!)

        } else {
            m_hitWall = true;
            newUp = m_prevNormal;

            glm::vec3 wallN = g.normal - m_prevNormal * glm::dot(g.normal, m_prevNormal);
            if (glm::length2(wallN) > 1e-8f) {
                wallN = glm::normalize(wallN);
                glm::vec3 vel = m_forward * m_speed;
                float into = glm::dot(vel, wallN);
                if (into > 0.0f) {
                    vel -= wallN * into;
                    vel *= m_tuning.wallImpactDecel;
                    m_speed = glm::length(vel) * (glm::dot(vel, m_forward) < 0.0f ? -1.0f : 1.0f);
                }
            }
        }
    } else {
        newUp = g.normal;
        nextPos = g.point;
    }

    m_prevNormal = g.normal;
    m_hasPrevNormal = true;
}

// ---------------------------------------------------------------------------
void Player::reorthogonalize() {
    m_forward = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));
    m_right   = glm::normalize(glm::cross(m_up, m_forward));
    m_forward = glm::cross(m_right, m_up);
}

glm::mat4 Player::basis() const {
    return glm::mat4(
        glm::vec4(m_right,   0.0f),
        glm::vec4(m_up,      0.0f),
        glm::vec4(m_forward, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

glm::mat4 Player::modelMatrix() const {
    return glm::translate(glm::mat4(1.0f), m_pos) * basis();
}

// ---------------------------------------------------------------------------
void Player::render(blur::Shader& shader, const std::vector<int>& boneLocs) const {
    const auto& bones = m_anim.boneMatrices();
    int boneCount     = (int)bones.size();
    for (int i = 0; i < boneCount && i < (int)boneLocs.size(); i++)
        glUniformMatrix4fv(boneLocs[i], 1, GL_FALSE, glm::value_ptr(bones[i]));

    glm::mat4 modelMat = modelMatrix();
    const auto& textures = m_anim.textures();

    for (const auto& mesh : m_anim.meshes()) {
        glm::mat4 meshModel = mesh.isSkinned ? modelMat : (modelMat * mesh.transform);
        shader.setMat4("uModel", meshModel);
        shader.setInt("uIsSkinned", mesh.isSkinned ? 1 : 0);

        bool hasTex = mesh.textureIndex >= 0 && mesh.textureIndex < (int)textures.size();
        shader.setInt("uHasTexture", hasTex ? 1 : 0);
        if (hasTex) {
            textures[mesh.textureIndex].bind(0);
            shader.setInt("uTexture", 0);
        }
        mesh.draw();
    }
}

} // namespace player
