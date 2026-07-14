#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <QObject>
#include <QVulkanInstance>
#include <vector>
#include <functional>
#include "core/config.h"

class QWidget;

namespace rtvk::render {

constexpr int kMaxFramesInFlight = 2;

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

    // Particle rendering
    void updateParticles(const std::vector<glm::vec3> &positions);

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

    QWidget *m_container = nullptr;
    HWND m_hwnd = nullptr;
    QVulkanInstance m_vulkanInstance;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE, m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = 0, m_presentFamily = 0;
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