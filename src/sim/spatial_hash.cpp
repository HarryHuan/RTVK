// spatial_hash.cpp -- GPU-accelerated spatial hashing
// Spatial hash construction and neighbor search now run on GPU via
// Vulkan compute shaders (see shaders/sim/spatial_hash.comp).
//
// CPU-side spatial hash helpers for debugging / fallback.

#include <cstdint>
#include <vector>
#include <cmath>

namespace rtvk::sim {

// CPU fallback: spatial hash cell index computation
uint32_t computeCellHash(float x, float y, float z, float cellSize, uint32_t tableSize)
{
    int cx = static_cast<int>(std::floor(x / cellSize));
    int cy = static_cast<int>(std::floor(y / cellSize));
    int cz = static_cast<int>(std::floor(z / cellSize));

    uint32_t h = static_cast<uint32_t>(cx) * 73856093u;
    h = h ^ (static_cast<uint32_t>(cy) * 19349663u);
    h = h ^ (static_cast<uint32_t>(cz) * 83492791u);
    return h % tableSize;
}

} // namespace rtvk::sim
