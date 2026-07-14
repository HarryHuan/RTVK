#include "pbd_solver.h"
#include <algorithm>
#include <cmath>

namespace rtvk::sim {

PBDSolver::PBDSolver(const SimParams &params)
    : m_params(params)
{
    // Initialize particles in a simple pile (cubic grid stacking)
    float r = params.particleRadius;
    float spacing = r * 2.1f;  // slight overlap for stable contact
    // Compute grid size from target particle count (roughly cubic)
    int n = static_cast<int>(std::cbrt(params.particleCount));
    if (n < 2) n = 2;
    int nx = n, ny = n, nz = n;

    m_particles.clear();
    m_particles.reserve(nx * ny * nz);

    for (int iy = 0; iy < ny; ++iy)
    {
        for (int iz = 0; iz < nz; ++iz)
        {
            for (int ix = 0; ix < nx; ++ix)
            {
                Particle p;
                p.position = glm::vec3(
                    (ix - nx * 0.5f) * spacing,
                    0.02f + iy * spacing,
                    (iz - nz * 0.5f) * spacing);
                p.velocity = glm::vec3(0.0f);
                p.predictedPosition = p.position;
                p.invMass = 1.0f;
                p.radius = r;
                m_particles.push_back(p);
            }
        }
    }
}

PBDSolver::~PBDSolver() = default;

void PBDSolver::step(float deltaTime)
{
    float dt = std::min(deltaTime, 0.033f);  // cap at ~30fps step
    applyForces(dt);
    predictPositions(dt);
    projectConstraints(m_params.constraintIterations);
    updateVelocities(dt);
}

// ── apply gravity & damping ─────────────────────────────────
void PBDSolver::applyForces(float dt)
{
    glm::vec3 gravityImpulse = m_params.gravity * dt;

    for (auto &p : m_particles)
    {
        if (p.invMass <= 0.0f)
        {
            continue;  // fixed / kinematic
        }
        p.velocity += gravityImpulse;

        // Air damping
        p.velocity *= (1.0f - 0.02f * dt);
    }
}

// ── predict positions ───────────────────────────────────────
void PBDSolver::predictPositions(float dt)
{
    for (auto &p : m_particles)
    {
        if (p.invMass <= 0.0f)
        {
            p.predictedPosition = p.position;
            continue;
        }
        p.predictedPosition = p.position + p.velocity * dt;
    }
}

// ── constraint projection ───────────────────────────────────
void PBDSolver::projectConstraints(uint32_t iterations)
{
    const float eps = 1e-6f;
    size_t count = m_particles.size();

    for (uint32_t iter = 0; iter < iterations; ++iter)
    {
        // ─── particle-particle distance constraints ───
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t j = i + 1; j < count; ++j)
            {
                Particle &a = m_particles[i];
                Particle &b = m_particles[j];
                float wSum = a.invMass + b.invMass;
                if (wSum < eps)
                {
                    continue;
                }

                glm::vec3 delta = a.predictedPosition - b.predictedPosition;
                float dist = glm::length(delta);
                float minDist = a.radius + b.radius;
                if (dist >= minDist || dist < eps)
                {
                    continue;
                }

                glm::vec3 n = delta / dist;
                float correction = (minDist - dist) / wSum;
                a.predictedPosition += n * correction * a.invMass;
                b.predictedPosition -= n * correction * b.invMass;
            }
        }

        // ─── ground plane collision ───
        for (auto &p : m_particles)
        {
            float groundY = m_params.domainMin.y + p.radius;
            if (p.predictedPosition.y < groundY)
            {
                p.predictedPosition.y = groundY;
            }

            // Clamp to domain walls (simple)
            p.predictedPosition.x = std::clamp(
                p.predictedPosition.x,
                m_params.domainMin.x + p.radius,
                m_params.domainMax.x - p.radius);
            p.predictedPosition.z = std::clamp(
                p.predictedPosition.z,
                m_params.domainMin.z + p.radius,
                m_params.domainMax.z - p.radius);
        }
    }
}

// ── update velocities ───────────────────────────────────────
void PBDSolver::updateVelocities(float dt)
{
    float invDt = 1.0f / dt;

    for (auto &p : m_particles)
    {
        if (p.invMass <= 0.0f)
        {
            continue;
        }
        p.velocity = (p.predictedPosition - p.position) * invDt;
        p.position = p.predictedPosition;
    }
}

} // namespace rtvk::sim