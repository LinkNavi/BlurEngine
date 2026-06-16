#define GLM_ENABLE_EXPERIMENTAL

#include "player.hpp"
#include "blur/shader.hpp"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

namespace player {

namespace {

float wrapAngle(float a) {
    while (a > glm::pi<float>()) a -= glm::two_pi<float>();
    while (a < -glm::pi<float>()) a += glm::two_pi<float>();
    return a;
}

} // namespace

Player::Player(blur::Model model, PlayerTuning tuning)
    : m_tuning(tuning), m_animModel(std::move(model)) {
    if (!m_animModel.animations().empty())
        m_animModel.playAnimation(0, true);
}

void Player::playAnimation(const std::string& name, bool loop) {
    m_animModel.playAnimation(name, loop);
}

void Player::playAnimation(int index, bool loop) {
    m_animModel.playAnimation(index, loop);
}

void Player::update(float dt, const glm::vec2& stickDir, const glm::vec3& camForward,
                     const glm::vec3& camRight, const GroundSample& ground) {
    m_hitWallThisFrame = false;

    float stickMag = glm::length(stickDir);

    // --- Wish position: camera-relative point a few units ahead ----------
    glm::vec3 stickDirWorld = camForward * stickDir.y + camRight * stickDir.x;
    glm::vec3 wishPos = m_position + stickDirWorld * m_tuning.lookAheadDist;

    // --- Steering: curve `forward` toward the wish point, car-style -------
    glm::vec3 toWish = wishPos - m_position;
    toWish -= m_up * glm::dot(toWish, m_up); // project onto ground plane

    if (stickMag > 1e-3f && glm::length(toWish) > 1e-4f) {
        glm::vec3 toWishN = glm::normalize(toWish);
        glm::vec3 fwdFlat = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));

        float targetYaw = std::atan2(toWishN.x, toWishN.z);
        float curYaw = std::atan2(fwdFlat.x, fwdFlat.z);
        float diff = wrapAngle(targetYaw - curYaw);

        // Faster = wider arcs (lower turn rate), like understeer.
        float speedT = glm::clamp(std::fabs(m_speed) / m_tuning.maxSpeed, 0.0f, 1.0f);
        float turnRate = glm::mix(m_tuning.minTurnRate, m_tuning.maxTurnRate, speedT);

        float maxStep = turnRate * dt;
        float step = glm::clamp(diff, -maxStep, maxStep);
        float newYaw = curYaw + step;
        m_forward = glm::vec3(std::sin(newYaw), 0.0f, std::cos(newYaw));
    }

    // --- Accel / decel / brake ----------------------------------------------
    // Braking kicks in when the stick wants a direction that's mostly
    // opposite our current heading - otherwise turning sharply while fast
    // would never feel like it costs you speed.
    float headingDot = 0.0f;
    if (stickMag > 1e-3f) {
        glm::vec3 fwdFlat = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));
        glm::vec3 wishFlat = glm::normalize(stickDirWorld - m_up * glm::dot(stickDirWorld, m_up));
        headingDot = glm::dot(fwdFlat, wishFlat);
    }

    if (stickMag > 0.05f) {
        if (headingDot < -0.3f && m_speed > 1.0f) {
            m_speed -= m_tuning.brakeDecel * dt;
        } else {
            m_speed += m_tuning.accel * stickMag * dt;
        }
    } else {
        float decel = m_tuning.decel * dt;
        m_speed = (m_speed > 0.0f) ? std::max(0.0f, m_speed - decel)
                                   : std::min(0.0f, m_speed + decel);
    }
    m_speed = glm::clamp(m_speed, -m_tuning.maxSpeed * 0.5f, m_tuning.maxSpeed);

    // --- Slope assist: gain speed downhill, lose it uphill ------------------
    float grade = glm::dot(glm::normalize(m_forward), glm::vec3(0, -1, 0)); // >0 means pointing downhill
    m_speed += grade * m_tuning.slopeAccelScale * dt;
    m_speed = glm::clamp(m_speed, -m_tuning.maxSpeed * 0.5f, m_tuning.maxSpeed);

    glm::vec3 velocity = m_forward * m_speed;
    glm::vec3 nextPos = m_position + velocity * dt;

    // --- SA2/Unleashed-style ground stick ------------------------------------
    // Sample the *current* surface normal directly underfoot (not a
    // predicted point ahead - that was the bug: a noisy or just-different
    // look-ahead sample could falsely read as a wall on an ordinary curve
    // and zero your velocity into the slope every frame, stalling you dead
    // on totally normal downhill turns).
    //
    // Instead: gravity/up always tracks the current ground normal. We only
    // ever treat something as "too steep to run on" when the normal swings
    // too far *since last frame* - i.e. an abrupt discontinuity, not a
    // smooth curve - and the allowed swing scales up with speed so a fast
    // Sonic can commit to banks/loops that would just be a flat-out wall
    // if you walked into them from a standstill.
    glm::vec3 targetUp = m_up;

    if (ground.grounded) {
        if (m_hasPrevGroundNormal) {
            float deltaAngle = std::acos(glm::clamp(glm::dot(m_prevGroundNormal, ground.normal), -1.0f, 1.0f));
            float threshold = glm::min(
                m_tuning.wallDeltaBase + m_tuning.wallDeltaPerSpeed * std::fabs(m_speed),
                m_tuning.wallDeltaMax);

            if (deltaAngle <= threshold) {
                targetUp = ground.normal;
                nextPos = ground.point;
            } else {
                // Abrupt change relative to our speed: treat it as a wall.
                // Bleed off the component of velocity driving into it and
                // keep gravity where it was instead of tilting up into it.
                m_hitWallThisFrame = true;
                targetUp = m_prevGroundNormal;

                glm::vec3 wallNormal = glm::normalize(ground.normal - m_prevGroundNormal * glm::dot(ground.normal, m_prevGroundNormal));
                if (glm::length(wallNormal) > 1e-4f) {
                    float into = glm::dot(velocity, wallNormal);
                    if (into > 0.0f) {
                        velocity -= wallNormal * into;
                        velocity *= m_tuning.wallImpactDecel;
                        m_speed = glm::length(velocity) * (glm::dot(velocity, m_forward) < 0.0f ? -1.0f : 1.0f);
                    }
                }
                nextPos = m_position + velocity * dt;
            }
        } else {
            targetUp = ground.normal;
            nextPos = ground.point;
        }

        m_prevGroundNormal = ground.normal;
        m_hasPrevGroundNormal = true;
    } else {
        // Airborne / no ground data: keep whatever up we had, don't snap.
        m_hasPrevGroundNormal = false;
    }

    m_position = nextPos;
    float upEaseRate = m_hitWallThisFrame ? 1e6f : m_tuning.upEase; // snap upright on impact instead of easing
    m_up = glm::normalize(glm::mix(m_up, targetUp, glm::clamp(upEaseRate * dt, 0.0f, 1.0f)));

    // Re-orthogonalize the basis against the (possibly tilted) up.
    m_forward = glm::normalize(m_forward - m_up * glm::dot(m_forward, m_up));
    m_right = glm::normalize(glm::cross(m_up, m_forward));
    m_forward = glm::cross(m_right, m_up);

    m_animModel.update(dt);
}

glm::mat4 Player::basis() const {
    return glm::mat4(
        glm::vec4(m_right, 0.0f),
        glm::vec4(m_up, 0.0f),
        glm::vec4(m_forward, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

glm::mat4 Player::modelMatrix() const {
    return glm::translate(glm::mat4(1.0f), m_position) * basis();
}

void Player::render(blur::Shader& shader, const std::vector<int>& boneLocs) const {
    const auto& bones = m_animModel.boneMatrices();
    int boneCount = (int)bones.size();
    for (int i = 0; i < boneCount && i < (int)boneLocs.size(); i++)
        glUniformMatrix4fv(boneLocs[i], 1, GL_FALSE, glm::value_ptr(bones[i]));

    glm::mat4 modelMat = modelMatrix();
    const auto& textures = m_animModel.textures();

    for (const auto& mesh : m_animModel.meshes()) {
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
