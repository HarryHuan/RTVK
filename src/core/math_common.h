#pragma once

#include <cmath>
#include <glm/glm.hpp>

namespace rtvk::math {

// Constants
constexpr float kPi = 3.14159265359f;
constexpr float kEpsilon = 1e-6f;

// Smooth kernel for SPH-like formulations
inline float poly6Kernel(float r, float h)
{
    if (r >= h) return 0.0f;
    float h2 = h * h;
    float diff = h2 - r * r;
    return (315.0f / (64.0f * kPi * std::pow(h, 9.0f))) * diff * diff * diff;
}

// Spiky gradient kernel
inline glm::vec3 spikyGradKernel(const glm::vec3 &rVec, float r, float h)
{
    if (r >= h || r < kEpsilon) return glm::vec3(0.0f);
    float diff = h - r;
    float coeff = -45.0f / (kPi * std::pow(h, 6.0f)) * diff * diff / r;
    return coeff * rVec;
}

} // namespace rtvk::math
