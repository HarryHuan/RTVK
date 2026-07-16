#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <QObject>
#include <QVulkanInstance>
#include <cstddef>
#include <functional>
#include <vector>
#include "core/config.h"

class QWidget;

namespace rtvk::render {

constexpr int kMaxFramesInFlight = 2;

// ─── GPU 粒子数据布局，必须匹配 shader std430 偏移 ────────────
struct GpuParticle
{
    float posX, posY, posZ, posPadding;
    float velX, velY, velZ, velPadding;
    float predX, predY, predZ, invMass;
    float radius;
    float _pad0;
    float _pad1;
    float _pad2;
};
static_assert(sizeof(GpuParticle) == 64, "GpuParticle must be 64 bytes");
static_assert(offsetof(GpuParticle, invMass) == 44, "GpuParticle invMass offset mismatch");
static_assert(offsetof(GpuParticle, radius) == 48, "GpuParticle radius offset mismatch");

struct CellEntry
{
    uint32_t start;
    uint32_t count;
};

struct ParticleCell
{
    uint32_t cellHash;
    uint32_t index;
};

class GranularVulkanWindow : public QObject {
    Q_OBJECT
public:
    explicit GranularVulkanWindow(QObject *parent = nullptr);
    ~GranularVulkanWindow() override;

    void initialize(QWidget *container);
    void resize();

    void setSimParams(const SimParams &p)  { m_simParams = p; }
    void setRenderParams(const RenderParams &p) { m_renderParams = p; }
    using FrameCallback = std::function<void(VkCommandBuffer, uint32_t)>;
    void setFrameCallback(FrameCallback cb) { m_frameCallback = std::move(cb); }

    // Particle rendering (positions from compute buffer)
    void updateParticles(const std::vector<glm::vec3> &positions);

    // ─── GPU Compute Simulation ──────────────────────────────────
    void initGpuSimulation(uint32_t particleCount, float particleRadius,
                           const SimParams &params);
    void gpuSimStep(float deltaTime);
    std::vector<glm::vec3> gpuSimGetPositions();
    size_t gpuSimParticleCount() const { return m_gpuParticleCount; }
    void uploadParticleData(const std::vector<GpuParticle> &data);

    QVulkanInstance *vulkanInstance() { return &m_vulkanInstance; }
    VkDevice device()          const { return m_device; }
    VkExtent2D extent()        const { return m_swapchainExtent; }
    VkFormat swapchainFormat() const { return m_swapchainFormat; }

signals:
    void vulkanReady();
    void vulkanError(const QString &msg);

private:
    void initVulkan();
    void cleanupVulkan();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void cleanupSwapchain();
    void createTrianglePipeline();
    void createParticlePipeline();
    void createParticleBuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void drawFrame();
    void drawParticles(VkCommandBuffer cb, uint32_t imageIndex);
    bool isDeviceSuitable(VkPhysicalDevice d);

    // ─── GPU Compute ─────────────────────────────────────────────
    void createComputeResources();
    void createComputePipelines();
    void createComputeBuffers();
    void createComputeDescriptorSets();
    void cleanupComputeResources();
    void dispatchSpatialHash(VkCommandBuffer cb);
    void dispatchConstraintProjection(VkCommandBuffer cb, uint32_t iterationCount);
    void dispatchIntegration(VkCommandBuffer cb, float dt);

    QWidget *m_container = nullptr;
    HWND m_hwnd = nullptr;
    QVulkanInstance m_vulkanInstance;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE, m_presentQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = 0, m_presentFamily = 0;
    uint32_t m_computeFamily = 0;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_swapchainExtent{};

    // Triangle pipeline
    VkPipelineLayout m_trianglePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_trianglePipeline = VK_NULL_HANDLE;

    // Particle pipeline
    VkPipelineLayout m_particlePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_particlePipeline = VK_NULL_HANDLE;
    VkBuffer m_particleVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleVertexMemory = VK_NULL_HANDLE;
    size_t m_particleCount = 0;
    bool m_particlePipelineReady = false;

    // ─── Compute resources ───────────────────────────────────────
    bool m_computeReady = false;
    uint32_t m_gpuParticleCount = 0;
    uint32_t m_hashTableSize = 0;
    float m_cellSize = 0.02f;

    // Compute pipeline layouts + pipelines
    VkPipelineLayout m_spatialHashPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_spatialHashPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_constraintPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_constraintPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_compactPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_compactPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_integratePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_integratePipeline = VK_NULL_HANDLE;

    // Compute buffers
    VkBuffer m_gpuParticleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_gpuParticleMemory = VK_NULL_HANDLE;
    VkBuffer m_gpuParticleStaging = VK_NULL_HANDLE;
    VkDeviceMemory m_gpuParticleStagingMemory = VK_NULL_HANDLE;

    VkBuffer m_hashTableBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_hashTableMemory = VK_NULL_HANDLE;
    VkBuffer m_particleCellBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleCellMemory = VK_NULL_HANDLE;
    VkBuffer m_cellOffsetBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cellOffsetMemory = VK_NULL_HANDLE;
    VkBuffer m_sortedParticleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sortedParticleMemory = VK_NULL_HANDLE;
    VkBuffer m_blockSumsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_blockSumsMemory = VK_NULL_HANDLE;

    // Compute descriptor pool + sets
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_spatialHashDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_compactDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_constraintDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_compactDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_integrateDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_spatialHashDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_constraintDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet m_integrateDescriptorSet = VK_NULL_HANDLE;

    // Compute command pool + buffer
    VkCommandPool m_computeCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_computeCommandBuffer = VK_NULL_HANDLE;
    VkFence m_computeFence = VK_NULL_HANDLE;

    // Other members
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores, m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;
    SimParams m_simParams;
    RenderParams m_renderParams;
    FrameCallback m_frameCallback;
    bool m_vulkanReady = false, m_framebufferResized = false;
};

} // namespace rtvk::render
