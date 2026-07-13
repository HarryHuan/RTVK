#include "pbd_solver.h"

namespace rtvk::sim {

PBDSolver::PBDSolver(const SimParams &params)
    : m_params(params)
{
    // TODO: Initialize particles based on scene config
}

PBDSolver::~PBDSolver() = default;

void PBDSolver::step(float deltaTime)
{
    applyForces(deltaTime);
    predictPositions(deltaTime);
    projectConstraints(m_params.constraintIterations);
    updateVelocities(deltaTime);
}

void PBDSolver::applyForces(float dt)
{
    // TODO: v_i += dt * f_ext / m_i
}

void PBDSolver::predictPositions(float dt)
{
    // TODO: p_i = x_i + dt * v_i
}

void PBDSolver::projectConstraints(uint32_t iterations)
{
    // TODO: Iterative constraint projection
}

void PBDSolver::updateVelocities(float dt)
{
    // TODO: v_i = (p_i - x_i) / dt
}

} // namespace rtvk::sim
