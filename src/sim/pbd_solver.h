#pragma once

#include "core/config.h"
#include <vector>
#include <glm/glm.hpp>

namespace rtvk::sim {

struct Particle
{
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 predictedPosition; // PBD intermediate
    float invMass = 1.0f;        // 1 / mass
    float radius = 0.01f;
};

class PBDSolver
{
public:
    explicit PBDSolver(const SimParams &params);
    ~PBDSolver();

    void step(float deltaTime);

    const std::vector<Particle> &particles() const { return m_particles; }
    size_t particleCount() const { return m_particles.size(); }

private:
    void applyForces(float dt);
    void predictPositions(float dt);
    void projectConstraints(uint32_t iterations);
    void updateVelocities(float dt);

    SimParams m_params;
    std::vector<Particle> m_particles;
};

} // namespace rtvk::sim
