#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include "sim/pbd_solver.h"

using namespace rtvk::sim;

// ── shared setup ─────────────────────────────────────────────
class PBDSolverTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rtvk::SimParams p;
        p.particleRadius = 0.05f;
        p.constraintIterations = 10;
        p.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        p.domainMin = glm::vec3(-2.0f, 0.0f, -2.0f);
        p.domainMax = glm::vec3(2.0f, 3.0f, 2.0f);
        m_params = p;
    }
    rtvk::SimParams m_params;
};

// ── 1. particle initialization ──────────────────────────────
TEST_F(PBDSolverTest, ParticleInitialization)
{
    PBDSolver solver(m_params);
    EXPECT_GT(solver.particleCount(), 0u);
    EXPECT_LT(solver.particleCount(), 2000u);  // reasonable pile size

    for (const auto &p : solver.particles())
    {
        // All within domain
        EXPECT_GE(p.position.x, m_params.domainMin.x - p.radius);
        EXPECT_LE(p.position.x, m_params.domainMax.x + p.radius);
        EXPECT_GE(p.position.y, 0.0f);
        EXPECT_LE(p.position.z, m_params.domainMax.z + p.radius);

        // Initial velocity is zero
        EXPECT_FLOAT_EQ(p.velocity.x, 0.0f);
        EXPECT_FLOAT_EQ(p.velocity.y, 0.0f);
        EXPECT_FLOAT_EQ(p.velocity.z, 0.0f);

        // invMass defaults to 1
        EXPECT_FLOAT_EQ(p.invMass, 1.0f);
    }
}

// ── 2. single step does not crash ───────────────────────────
TEST_F(PBDSolverTest, SingleStep)
{
    PBDSolver solver(m_params);
    solver.step(1.0f / 60.0f);

    for (const auto &p : solver.particles())
    {
        // Position should still be finite
        EXPECT_FALSE(std::isnan(p.position.x));
        EXPECT_FALSE(std::isnan(p.position.y));
        EXPECT_FALSE(std::isnan(p.position.z));
        EXPECT_FALSE(std::isinf(p.position.x));
    }
}

// ── 3. ground collision ─────────────────────────────────────
TEST_F(PBDSolverTest, GroundCollision)
{
    m_params.gravity = glm::vec3(0.0f, -50.0f, 0.0f);  // strong gravity
    m_params.constraintIterations = 20;
    PBDSolver solver(m_params);

    for (int i = 0; i < 10; ++i)
    {
        solver.step(1.0f / 60.0f);
    }

    for (const auto &p : solver.particles())
    {
        float minY = m_params.domainMin.y + p.radius;  // ground = y=0
        EXPECT_GE(p.position.y, minY - 0.001f)  // tiny tolerance
            << "Particle below ground at y=" << p.position.y;
    }
}

// ── 4. distance constraint prevents overlap ─────────────────
TEST_F(PBDSolverTest, NoExcessiveOverlap)
{
    m_params.gravity = glm::vec3(0.0f, -30.0f, 0.0f);
    m_params.constraintIterations = 30;
    PBDSolver solver(m_params);

    for (int i = 0; i < 5; ++i)
    {
        solver.step(1.0f / 60.0f);
    }

    const auto &parts = solver.particles();
    for (size_t i = 0; i < parts.size(); ++i)
    {
        for (size_t j = i + 1; j < parts.size(); ++j)
        {
            float dist = glm::distance(
                parts[i].position, parts[j].position);
            float minDist = parts[i].radius + parts[j].radius;
            // Allow up to 10% overlap (PBD is approximate)
            EXPECT_GT(dist, minDist * 0.9f)
                << "Particles " << i << " and " << j
                << " overlap at dist=" << dist;
        }
    }
}

// ── 5. velocity integration consistency ─────────────────────
TEST_F(PBDSolverTest, VelocityIntegration)
{
    // Single particle under gravity only
    m_params.particleRadius = 0.1f;
    PBDSolver solver(m_params);

    // Get the first particle
    const auto &p0 = solver.particles()[0];
    float y0 = p0.position.y;

    solver.step(1.0f / 60.0f);

    const auto &p1 = solver.particles()[0];
    // After one step, y should decrease (gravity pulls down)
    // unless clamped by ground
    if (y0 > m_params.domainMin.y + p0.radius + 0.01f)
    {
        EXPECT_LT(p1.position.y, y0)
            << "Particle should fall under gravity";
    }
}