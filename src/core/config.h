#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace rtvk {

// ── Simulation parameters ────────────────────────
struct SimParams
{
    // Particle count
    uint32_t particleCount = 5000;

    // Material properties
    float friction = 0.5f;          // Coulomb friction
    float restitution = 0.1f;       // Bounce factor
    float adhesion = 0.0f;          // ACL: adhesion/cohesion
    float particleRadius = 0.01f;   // meters

    // PBD solver
    uint32_t constraintIterations = 5;
    float timeStep = 1.0f / 60.0f;

    // Gravity
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    // Domain
    glm::vec3 domainMin = glm::vec3(-1.0f);
    glm::vec3 domainMax = glm::vec3(1.0f);
};

// ── Render parameters ────────────────────────────
struct RenderParams
{
    bool ssrEnabled = true;       // Screen-space rendering
    bool aoEnabled = true;        // Ambient occlusion
    bool shadowEnabled = true;
    bool fogEnabled = false;
    bool debugOverlay = false;

    // Material
    enum class Material : uint8_t {
        Sand = 0,
        Soil = 1,
        Coal = 2
    };
    Material material = Material::Sand;
};

} // namespace rtvk
