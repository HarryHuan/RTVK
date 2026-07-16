#include "vulkan_window.h"
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <QWidget>
#include <set>
#include <cstring>
#include <stdexcept>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace rtvk::render
{
    // ── helpers ──────────────────────────────────────────────────
    static std::vector<char> readFile(const QString &relPath)
    {
        QString fullPath = QCoreApplication::applicationDirPath() + "/" + relPath;
        QFile f(fullPath);
        if (!f.open(QIODevice::ReadOnly))
        {
            throw std::runtime_error("no shader: " + fullPath.toStdString());
        }

        QByteArray d = f.readAll();
        return {d.begin(), d.end()};
    }

    static VkShaderModule createShader(VkDevice d, const std::vector<char> &code)
    {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t *>(code.data());
        VkShaderModule m;
        if (vkCreateShaderModule(d, &ci, nullptr, &m) != VK_SUCCESS)
        {
            throw std::runtime_error("shader module");
        }

        return m;
    }

    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        return DefWindowProc(h, m, w, l);
    }

    static uint32_t findMemoryType(
        VkPhysicalDevice phys,
        uint32_t typeFilter,
        VkMemoryPropertyFlags props)
    {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {

            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & props) == props)
            {
                return i;
            }
        }

        throw std::runtime_error("no suitable memory type");
    }

    static void createBuffer(
        VkDevice device,
        VkPhysicalDevice phys,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkBuffer &buffer,
        VkDeviceMemory &memory)
    {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};

        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, &buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("buffer create");
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, buffer, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};

        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, props);
        if (vkAllocateMemory(device, &ai, nullptr, &memory) != VK_SUCCESS)
        {
            throw std::runtime_error("buffer memory");
        }

        vkBindBufferMemory(device, buffer, memory, 0);
    }

    // ── ctor / dtor ──────────────────────────────────────────────
    GranularVulkanWindow::GranularVulkanWindow(QObject *p) : QObject(p) {}

    GranularVulkanWindow::~GranularVulkanWindow() { cleanupVulkan(); }

    // ── initialize ───────────────────────────────────────────────
    void GranularVulkanWindow::initialize(QWidget *container)
    {
        m_container = container;
        HINSTANCE hi = GetModuleHandle(nullptr);

        WNDCLASSEX wc = {
            sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hi,
            nullptr, nullptr, nullptr, nullptr, L"RTVK_Vulkan", nullptr};

        RegisterClassEx(&wc);
        RECT r;
        GetClientRect((HWND)container->winId(), &r);
        m_hwnd = CreateWindowExW(
            0, L"RTVK_Vulkan", L"", WS_CHILD | WS_VISIBLE,
            0, 0, r.right - r.left, r.bottom - r.top,
            (HWND)container->winId(), nullptr, hi, nullptr);
        QTimer::singleShot(200, this, [this]
                           { initVulkan(); });
    }

    void GranularVulkanWindow::resize()
    {
        if (!m_hwnd || !m_container)
            return;
        RECT r;
        GetClientRect((HWND)m_container->winId(), &r);
        SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_vulkanReady)
            m_framebufferResized = true;
    }

    // ── update particle positions ────────────────────────────────
    void GranularVulkanWindow::updateParticles(
        const std::vector<glm::vec3> &positions)
    {
        if (!m_particlePipelineReady || positions.empty())
            return;
        size_t byteSize = positions.size() * sizeof(glm::vec3);
        if (byteSize > m_particleCount * sizeof(glm::vec3))
        {
            // Reallocate buffer
            vkDeviceWaitIdle(m_device);
            vkDestroyBuffer(m_device, m_particleVertexBuffer, nullptr);
            vkFreeMemory(m_device, m_particleVertexMemory, nullptr);
            m_particleVertexBuffer = VK_NULL_HANDLE;
            m_particleVertexMemory = VK_NULL_HANDLE;
            m_particleCount = 0;

            // Round up to avoid frequent reallocation
            size_t cap = std::max(positions.size(), size_t(256));
            createBuffer(
                m_device, m_physicalDevice,
                cap * sizeof(glm::vec3),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_particleVertexBuffer, m_particleVertexMemory);
            m_particleCount = cap;
        }

        void *data;
        vkMapMemory(
            m_device, m_particleVertexMemory, 0, byteSize, 0, &data);
        std::memcpy(data, positions.data(), byteSize);
        vkUnmapMemory(m_device, m_particleVertexMemory);
    }

    // ── Vulkan init ──────────────────────────────────────────────
    void GranularVulkanWindow::initVulkan()
    {
        try
        {
            createInstance();
            createSurface();
            pickPhysicalDevice();
            createLogicalDevice();
            createSwapchain();
            createTrianglePipeline();
            createParticlePipeline();
            createCommandPool();
            createCommandBuffers();
            createSyncObjects();
            m_vulkanReady = true;
            emit vulkanReady();
            auto *t = new QTimer(this);
            connect(t, &QTimer::timeout, this, [this]
                    { drawFrame(); });
            t->start(16);
            qDebug() << "[RTVK] Vulkan Ready";
        }

        catch (const std::exception &e)
        {
            qCritical() << "[RTVK]" << e.what();
            emit vulkanError(e.what());
        }
    }

    void GranularVulkanWindow::cleanupVulkan()
    {
        if (!m_device)
            return;
        vkDeviceWaitIdle(m_device);
        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }

        vkDestroyPipeline(m_device, m_trianglePipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_trianglePipelineLayout, nullptr);
        if (m_particlePipelineReady)
        {
            vkDestroyPipeline(m_device, m_particlePipeline, nullptr);
            vkDestroyPipelineLayout(m_device, m_particlePipelineLayout, nullptr);
            vkDestroyBuffer(m_device, m_particleVertexBuffer, nullptr);
            vkFreeMemory(m_device, m_particleVertexMemory, nullptr);
        }

        cleanupComputeResources();
        cleanupSwapchain();
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        vkDestroyDevice(m_device, nullptr);
        if (m_surface)
        {
            vkDestroySurfaceKHR(m_vulkanInstance.vkInstance(), m_surface, nullptr);
        }

        m_device = VK_NULL_HANDLE;
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void GranularVulkanWindow::createInstance()
    {
        m_vulkanInstance.setExtensions(
            {"VK_KHR_surface", "VK_KHR_win32_surface"});
        m_vulkanInstance.setApiVersion(QVersionNumber(1, 3));
        if (!m_vulkanInstance.create())
        {
            throw std::runtime_error("instance");
        }
    }

    void GranularVulkanWindow::createSurface()
    {
        VkWin32SurfaceCreateInfoKHR si{
            VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};

        si.hinstance = GetModuleHandle(nullptr);
        si.hwnd = m_hwnd;
        if (vkCreateWin32SurfaceKHR(
                m_vulkanInstance.vkInstance(), &si, nullptr, &m_surface) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("surface");
        }
    }

    void GranularVulkanWindow::pickPhysicalDevice()
    {
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(
            m_vulkanInstance.vkInstance(), &n, nullptr);
        std::vector<VkPhysicalDevice> ds(n);
        vkEnumeratePhysicalDevices(
            m_vulkanInstance.vkInstance(), &n, ds.data());
        for (auto d : ds)
        {
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(d, &p);
            if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                isDeviceSuitable(d))
            {
                m_physicalDevice = d;
                return;
            }
        }

        for (auto d : ds)
        {
            if (isDeviceSuitable(d))
            {
                m_physicalDevice = d;
                return;
            }
        }

        throw std::runtime_error("no suitable GPU");
    }

    bool GranularVulkanWindow::isDeviceSuitable(VkPhysicalDevice d)
    {
        uint32_t n;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
        std::vector<VkQueueFamilyProperties> qs(n);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, qs.data());
        m_graphicsFamily = m_presentFamily = UINT32_MAX;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_graphicsFamily = i;
            }

            VkBool32 ps;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &ps);
            if (ps)
                m_presentFamily = i;
            if (m_graphicsFamily != UINT32_MAX &&
                m_presentFamily != UINT32_MAX)
            {
                break;
            }
        }

        m_computeFamily = UINT32_MAX;
        for (uint32_t i = 0; i < n; ++i)
        {
            if ((qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                m_computeFamily = i;
                break;
            }
        }

        if (m_computeFamily == UINT32_MAX)
        {
            m_computeFamily = m_graphicsFamily;
        }

        uint32_t fc, pc;
        vkGetPhysicalDeviceSurfaceFormatsKHR(d, m_surface, &fc, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            d, m_surface, &pc, nullptr);
        return m_graphicsFamily != UINT32_MAX &&
               m_presentFamily != UINT32_MAX && fc > 0 && pc > 0;
    }

    void GranularVulkanWindow::createLogicalDevice()
    {
        std::set<uint32_t> uf{m_graphicsFamily, m_presentFamily, m_computeFamily};

        std::vector<VkDeviceQueueCreateInfo> qis;
        float prio = 1.0f;
        for (auto f : uf)
        {
            VkDeviceQueueCreateInfo qi{
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};

            qi.queueFamilyIndex = f;
            qi.queueCount = 1;
            qi.pQueuePriorities = &prio;
            qis.push_back(qi);
        }

        std::vector<const char *> exts{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkPhysicalDeviceDynamicRenderingFeatures dr{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};

        dr.dynamicRendering = VK_TRUE;
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

        ci.pNext = &dr;
        ci.queueCreateInfoCount = (uint32_t)qis.size();
        ci.pQueueCreateInfos = qis.data();
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();
        if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("logical device");
        }

        vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
        vkGetDeviceQueue(m_device, m_computeFamily, 0, &m_computeQueue);
    }

    // ── swapchain ────────────────────────────────────────────────
    void GranularVulkanWindow::createSwapchain()
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_physicalDevice, m_surface, &caps);
        VkExtent2D ext = caps.currentExtent;
        if (ext.width == UINT32_MAX)
        {
            ext = {1280, 720};
        }

        m_swapchainExtent = ext;
        uint32_t n;
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            m_physicalDevice, m_surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            m_physicalDevice, m_surface, &n, fmts.data());
        m_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
        for (auto &f : fmts)
        {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                m_swapchainFormat = f.format;
                break;
            }
        }

        VkSwapchainCreateInfoKHR ci{
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};

        ci.surface = m_surface;
        ci.minImageCount = std::max(2u, caps.minImageCount);
        ci.imageFormat = m_swapchainFormat;
        ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        ci.imageExtent = ext;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        uint32_t families[] = {m_graphicsFamily, m_presentFamily};

        if (m_graphicsFamily != m_presentFamily)
        {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = families;
        }

        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        ci.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapchain) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("swapchain");
        }

        uint32_t ic;
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &ic, nullptr);
        m_swapchainImages.resize(ic);
        vkGetSwapchainImagesKHR(
            m_device, m_swapchain, &ic, m_swapchainImages.data());
        m_swapchainImageViews.resize(ic);
        for (uint32_t i = 0; i < ic; ++i)
        {
            VkImageViewCreateInfo vi{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};

            vi.image = m_swapchainImages[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = m_swapchainFormat;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount =
                vi.subresourceRange.layerCount = 1;
            vkCreateImageView(
                m_device, &vi, nullptr, &m_swapchainImageViews[i]);
        }
    }

    void GranularVulkanWindow::cleanupSwapchain()
    {
        for (auto v : m_swapchainImageViews)
        {
            vkDestroyImageView(m_device, v, nullptr);
        }

        m_swapchainImageViews.clear();
        if (m_swapchain)
        {
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    // ── triangle pipeline ────────────────────────────────────────
    void GranularVulkanWindow::createTrianglePipeline()
    {
        auto vertCode = readFile("shaders/common/fullscreen.vert.spv");
        auto fragCode = readFile("shaders/common/fullscreen.frag.spv");
        VkShaderModule vs = createShader(m_device, vertCode);
        VkShaderModule fs = createShader(m_device, fragCode);

        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr}};

        VkPipelineVertexInputStateCreateInfo vis{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

        VkPipelineInputAssemblyStateCreateInfo ias{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};

        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vps{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};

        vps.viewportCount = vps.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};

        rs.lineWidth = 1.0f;
        rs.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};

        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};

        cba.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

        cbs.attachmentCount = 1;
        cbs.pAttachments = &cba;

        VkPushConstantRange pcr{
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4};

        VkPipelineLayoutCreateInfo pli{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcr;
        vkCreatePipelineLayout(
            m_device, &pli, nullptr, &m_trianglePipelineLayout);

        std::vector<VkDynamicState> ds{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dys{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

        dys.dynamicStateCount = (uint32_t)ds.size();
        dys.pDynamicStates = ds.data();

        VkPipelineRenderingCreateInfo rci{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

        rci.colorAttachmentCount = 1;
        rci.pColorAttachmentFormats = &m_swapchainFormat;

        VkGraphicsPipelineCreateInfo pci{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

        pci.pNext = &rci;
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState = &vps;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cbs;
        pci.pDynamicState = &dys;
        pci.layout = m_trianglePipelineLayout;
        if (vkCreateGraphicsPipelines(
                m_device, VK_NULL_HANDLE, 1, &pci, nullptr,
                &m_trianglePipeline) != VK_SUCCESS)

        {

            throw std::runtime_error("triangle pipeline");
        }

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
    }

    // ── particle pipeline ────────────────────────────────────────
    void GranularVulkanWindow::createParticlePipeline()
    {
        auto vertCode = readFile("shaders/particle/particle.vert.spv");
        auto fragCode = readFile("shaders/particle/particle.frag.spv");
        VkShaderModule vs = createShader(m_device, vertCode);
        VkShaderModule fs = createShader(m_device, fragCode);

        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main",
             nullptr}};

        // Vertex input: single vec3 per particle

        VkVertexInputBindingDescription binding{
            0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX};

        VkVertexInputAttributeDescription attr{
            0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};

        VkPipelineVertexInputStateCreateInfo vis{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

        vis.vertexBindingDescriptionCount = 1;
        vis.pVertexBindingDescriptions = &binding;
        vis.vertexAttributeDescriptionCount = 1;
        vis.pVertexAttributeDescriptions = &attr;

        VkPipelineInputAssemblyStateCreateInfo ias{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};

        ias.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

        VkPipelineViewportStateCreateInfo vps{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};

        vps.viewportCount = vps.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};

        rs.lineWidth = 1.0f;
        rs.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};

        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};

        cba.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};

        cbs.attachmentCount = 1;
        cbs.pAttachments = &cba;

        // Push constant: mat4 mvp + float pointSize
        VkPushConstantRange pcr{
            VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(glm::mat4) + sizeof(float)};

        VkPipelineLayoutCreateInfo pli{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcr;
        vkCreatePipelineLayout(
            m_device, &pli, nullptr, &m_particlePipelineLayout);

        std::vector<VkDynamicState> ds{
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dys{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};

        dys.dynamicStateCount = (uint32_t)ds.size();
        dys.pDynamicStates = ds.data();

        VkPipelineRenderingCreateInfo rci{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

        rci.colorAttachmentCount = 1;
        rci.pColorAttachmentFormats = &m_swapchainFormat;

        VkGraphicsPipelineCreateInfo pci{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

        pci.pNext = &rci;
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState = &vps;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cbs;
        pci.pDynamicState = &dys;
        pci.layout = m_particlePipelineLayout;
        if (vkCreateGraphicsPipelines(
                m_device, VK_NULL_HANDLE, 1, &pci, nullptr,
                &m_particlePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("particle pipeline");
        }

        vkDestroyShaderModule(m_device, vs, nullptr);
        vkDestroyShaderModule(m_device, fs, nullptr);
        m_particlePipelineReady = true;
    }

    // ── commands & sync ──────────────────────────────────────────
    void GranularVulkanWindow::createCommandPool()
    {
        VkCommandPoolCreateInfo ci{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = m_graphicsFamily;
        vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool);
    }

    void GranularVulkanWindow::createCommandBuffers()
    {
        m_commandBuffers.resize(kMaxFramesInFlight);

        VkCommandBufferAllocateInfo ai{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        ai.commandPool = m_commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = kMaxFramesInFlight;
        vkAllocateCommandBuffers(
            m_device, &ai, m_commandBuffers.data());
    }

    void GranularVulkanWindow::createSyncObjects()
    {
        m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
        m_renderFinishedSemaphores.resize(kMaxFramesInFlight);
        m_inFlightFences.resize(kMaxFramesInFlight);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kMaxFramesInFlight; ++i)
        {
            vkCreateSemaphore(
                m_device, &si, nullptr, &m_imageAvailableSemaphores[i]);
            vkCreateSemaphore(
                m_device, &si, nullptr, &m_renderFinishedSemaphores[i]);
            vkCreateFence(
                m_device, &fi, nullptr, &m_inFlightFences[i]);
        }
    }

    // ── draw frame ───────────────────────────────────────────────
    void GranularVulkanWindow::drawFrame()
    {
        if (!m_vulkanReady)
            return;
        if (m_framebufferResized)
        {
            vkDeviceWaitIdle(m_device);
            cleanupSwapchain();
            createSwapchain();
            createTrianglePipeline();
            createParticlePipeline();
            m_framebufferResized = false;
            return;
        }

        vkWaitForFences(
            m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE,
            UINT64_MAX);
        uint32_t ii;
        VkResult r = vkAcquireNextImageKHR(
            m_device, m_swapchain, UINT64_MAX,
            m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE,
            &ii);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        {
            m_framebufferResized = true;
            return;
        }

        vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
        VkCommandBuffer cb = m_commandBuffers[m_currentFrame];
        vkResetCommandBuffer(cb, 0);

        VkCommandBufferBeginInfo bi{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        vkBeginCommandBuffer(cb, &bi);

        // Transition to color attachment

        VkImageMemoryBarrier barrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = m_swapchainImages[ii];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount =
            barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &barrier);

        // Begin dynamic rendering

        VkRenderingAttachmentInfo att{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};

        att.imageView = m_swapchainImageViews[ii];
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.clearValue.color = {{0.1f, 0.1f, 0.15f, 1.0f}};

        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};

        ri.renderArea = {{0, 0}, m_swapchainExtent};

        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &att;
        vkCmdBeginRendering(cb, &ri);

        // Draw triangle
        vkCmdBindPipeline(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline);

        VkViewport vp{
            0, 0, (float)m_swapchainExtent.width,
            (float)m_swapchainExtent.height, 0, 1};

        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D sc{{0, 0}, m_swapchainExtent};

        vkCmdSetScissor(cb, 0, 1, &sc);
        float pcColor[4] = {0.2f, 0.6f, 0.8f, 1.0f};

        vkCmdPushConstants(
            cb, m_trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(pcColor), pcColor);
        vkCmdDraw(cb, 3, 1, 0, 0);

        // Draw particles
        drawParticles(cb, ii);
        if (m_frameCallback)
            m_frameCallback(cb, ii);
        vkCmdEndRendering(cb);

        // Transition to present
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(
            cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &barrier);
        vkEndCommandBuffer(cb);
        VkPipelineStageFlags ws =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores =
            &m_imageAvailableSemaphores[m_currentFrame];
        si.pWaitDstStageMask = &ws;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores =
            &m_renderFinishedSemaphores[m_currentFrame];
        vkQueueSubmit(
            m_graphicsQueue, 1, &si, m_inFlightFences[m_currentFrame]);
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores =
            &m_renderFinishedSemaphores[m_currentFrame];
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_swapchain;
        pi.pImageIndices = &ii;
        vkQueuePresentKHR(m_presentQueue, &pi);
        m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
    }

    // ── draw particles ───────────────────────────────────────────
    void GranularVulkanWindow::drawParticles(
        VkCommandBuffer cb, uint32_t /*imageIndex*/)
    {
        if (!m_particlePipelineReady || !m_particleVertexBuffer)
        {
            return;
        }
        // Compute MVP: look-at from above-right
        glm::mat4 proj = glm::perspective(
            glm::radians(45.0f),
            (float)m_swapchainExtent.width /
                (float)m_swapchainExtent.height,
            0.1f, 10.0f);
        // Vulkan 的视口 Y 轴方向与 GLM 默认投影相反，需要翻转投影矩阵的 Y 分量。
        proj[1][1] *= -1.0f;
        glm::mat4 view = glm::lookAt(
            glm::vec3(0.8f, 1.5f, 1.2f),  // camera position
            glm::vec3(0.0f, 0.5f, 0.0f),  // look at center of pile
            glm::vec3(0.0f, 1.0f, 0.0f)); // up
        glm::mat4 mvp = proj * view;
        struct PushData
        {
            glm::mat4 mvp;
            float pointSize;
        } push;
        push.mvp = mvp;
        push.pointSize = 18.0f; // pixels
        vkCmdBindPipeline(
            cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline);
        vkCmdPushConstants(
            cb, m_particlePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
            sizeof(PushData), &push);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(
            cb, 0, 1, &m_particleVertexBuffer, &offset);
        vkCmdDraw(cb, (uint32_t)m_particleCount, 1, 0, 0);
    }

    // ============================================================================
    //  GPU Compute Simulation
    // ============================================================================
    static uint32_t nextPow2(uint32_t v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return v + 1;
    }

    // -- create compute resources ------------------------------------------------
    void GranularVulkanWindow::createComputeResources()
    {
        createComputePipelines();
        createComputeDescriptorSets();
        m_computeReady = true;
        qDebug() << "[RTVK] Compute pipelines ready";
    }

    // -- create compute pipelines -------------------------------------------------
    void GranularVulkanWindow::createComputePipelines()
    {
        auto spatialCode = readFile("shaders/sim/spatial_hash.comp.spv");
        auto constraintCode = readFile("shaders/sim/constraint.comp.spv");
        auto integrateCode = readFile("shaders/sim/integrate.comp.spv");
        VkShaderModule spatialShader = createShader(m_device, spatialCode);
        VkShaderModule constraintShader = createShader(m_device, constraintCode);
        VkShaderModule integrateShader = createShader(m_device, integrateCode);

        // spatial_hash descriptor layout: 4 SSBO bindings
        {
            VkDescriptorSetLayoutBinding bindings[4]{};

            for (int i = 0; i < 4; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }

            VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

            dslci.bindingCount = 4;
            dslci.pBindings = bindings;
            vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_spatialHashDescriptorSetLayout);
            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

            plci.setLayoutCount = 1;
            plci.pSetLayouts = &m_spatialHashDescriptorSetLayout;
            vkCreatePipelineLayout(m_device, &plci, nullptr, &m_spatialHashPipelineLayout);
        }

        // constraint descriptor layout: 3 SSBO bindings (particles, hashTable, sortedList)
        {
            VkDescriptorSetLayoutBinding bindings[3]{};

            for (int i = 0; i < 3; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }

            VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

            dslci.bindingCount = 3;
            dslci.pBindings = bindings;
            vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_constraintDescriptorSetLayout);
            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

            plci.setLayoutCount = 1;
            plci.pSetLayouts = &m_constraintDescriptorSetLayout;
            vkCreatePipelineLayout(m_device, &plci, nullptr, &m_constraintPipelineLayout);
        }

        // compact descriptor layout: 5 SSBO bindings (hashTable, blockSums, unsortedCells, slots, sortedOut) + push constants
        {
            VkDescriptorSetLayoutBinding bindings[5]{};

            for (int i = 0; i < 5; ++i)
            {
                bindings[i].binding = i;
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            }

            VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

            dslci.bindingCount = 5;
            dslci.pBindings = bindings;
            vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_compactDescriptorSetLayout);
            VkPushConstantRange pcRange{};

            pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pcRange.offset = 0;
            pcRange.size = sizeof(uint32_t); // phase
            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

            plci.setLayoutCount = 1;
            plci.pSetLayouts = &m_compactDescriptorSetLayout;
            plci.pushConstantRangeCount = 1;
            plci.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(m_device, &plci, nullptr, &m_compactPipelineLayout);
        }

        // integrate descriptor layout: 1 SSBO + push constants
        {
            VkDescriptorSetLayoutBinding binding{};

            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

            dslci.bindingCount = 1;
            dslci.pBindings = &binding;
            vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_integrateDescriptorSetLayout);
            VkPushConstantRange pcRange{};

            pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pcRange.offset = 0;
            pcRange.size = 36; // float + vec3(align 16) + float + uint
            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

            plci.setLayoutCount = 1;
            plci.pSetLayouts = &m_integrateDescriptorSetLayout;
            plci.pushConstantRangeCount = 1;
            plci.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(m_device, &plci, nullptr, &m_integratePipelineLayout);
        }

        // Specialization constants (shared: 0=particleCount, 1=hashTableSize, 2=cellSize,

        // 3=domainMinY, 4=domainMaxY, 5=domainMinX, 6=domainMaxX, 7=domainMinZ, 8=domainMaxZ)
        VkSpecializationMapEntry specEntries[9]{};

        for (int i = 0; i < 9; ++i)
        {
            specEntries[i].constantID = i;
            specEntries[i].offset = i * sizeof(uint32_t);
            specEntries[i].size = sizeof(uint32_t);
        }

        struct SpecData
        {
            uint32_t particleCount;
            uint32_t hashTableSize;
            float cellSize;
            float domainMinY, domainMaxY;
            float domainMinX, domainMaxX;
            float domainMinZ, domainMaxZ;
        } specData{};

        // Populate specialization constants from current simulation state
        specData.particleCount = m_gpuParticleCount;
        specData.hashTableSize = m_hashTableSize;
        specData.cellSize = m_cellSize;
        specData.domainMinY = m_simParams.domainMin.y;
        specData.domainMaxY = m_simParams.domainMax.y;
        specData.domainMinX = m_simParams.domainMin.x;
        specData.domainMaxX = m_simParams.domainMax.x;
        specData.domainMinZ = m_simParams.domainMin.z;
        specData.domainMaxZ = m_simParams.domainMax.z;
        VkSpecializationInfo specInfo{};

        specInfo.mapEntryCount = 9;
        specInfo.pMapEntries = specEntries;
        specInfo.dataSize = sizeof(SpecData);
        specInfo.pData = &specData;

        // spatial_hash pipeline
        {
            VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

            cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = spatialShader;
            cpci.stage.pName = "main";
            cpci.stage.pSpecializationInfo = &specInfo;
            cpci.layout = m_spatialHashPipelineLayout;
            vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_spatialHashPipeline);
        }

        // constraint pipeline
        {
            VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

            cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = constraintShader;
            cpci.stage.pName = "main";
            cpci.stage.pSpecializationInfo = &specInfo;
            cpci.layout = m_constraintPipelineLayout;
            vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_constraintPipeline);
        }

        // compact pipeline (hashTableSize + particleCount spec constants)
        {
            auto compactCode = readFile("shaders/sim/compact.comp.spv");
            VkShaderModule compactShader = createShader(m_device, compactCode);
            VkSpecializationMapEntry compactEntries[2]{};

            compactEntries[0].constantID = 0;
            compactEntries[0].offset = 0;
            compactEntries[0].size = sizeof(uint32_t);
            compactEntries[1].constantID = 1;
            compactEntries[1].offset = sizeof(uint32_t);
            compactEntries[1].size = sizeof(uint32_t);
            struct CompactSpec
            {
                uint32_t hashTableSize;
                uint32_t particleCount;
            } compactSpecData;
            compactSpecData.hashTableSize = specData.hashTableSize;
            compactSpecData.particleCount = specData.particleCount;
            VkSpecializationInfo compactSpec{};

            compactSpec.mapEntryCount = 2;
            compactSpec.pMapEntries = compactEntries;
            compactSpec.dataSize = sizeof(CompactSpec);
            compactSpec.pData = &compactSpecData;
            VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

            cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = compactShader;
            cpci.stage.pName = "main";
            cpci.stage.pSpecializationInfo = &compactSpec;
            cpci.layout = m_compactPipelineLayout;
            vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_compactPipeline);
            vkDestroyShaderModule(m_device, compactShader, nullptr);
        }

        // integrate pipeline (only particleCount spec constant)
        {
            VkSpecializationMapEntry pcEntry{};

            pcEntry.constantID = 0;
            pcEntry.offset = 0;
            pcEntry.size = sizeof(uint32_t);
            VkSpecializationInfo pcSpec{};

            pcSpec.mapEntryCount = 1;
            pcSpec.pMapEntries = &pcEntry;
            pcSpec.dataSize = sizeof(uint32_t);
            pcSpec.pData = &specData.particleCount;
            VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

            cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpci.stage.module = integrateShader;
            cpci.stage.pName = "main";
            cpci.stage.pSpecializationInfo = &pcSpec;
            cpci.layout = m_integratePipelineLayout;
            vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_integratePipeline);
        }

        vkDestroyShaderModule(m_device, spatialShader, nullptr);
        vkDestroyShaderModule(m_device, constraintShader, nullptr);
        vkDestroyShaderModule(m_device, integrateShader, nullptr);
    }

    // -- create compute descriptor sets -------------------------------------------
    void GranularVulkanWindow::createComputeDescriptorSets()
    {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 20}

        };

        VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};

        dpci.maxSets = 4;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(m_device, &dpci, nullptr, &m_computeDescriptorPool);
        VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};

        dsai.descriptorPool = m_computeDescriptorPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &m_spatialHashDescriptorSetLayout;
        vkAllocateDescriptorSets(m_device, &dsai, &m_spatialHashDescriptorSet);
        dsai.pSetLayouts = &m_constraintDescriptorSetLayout;
        vkAllocateDescriptorSets(m_device, &dsai, &m_constraintDescriptorSet);
        dsai.pSetLayouts = &m_compactDescriptorSetLayout;
        vkAllocateDescriptorSets(m_device, &dsai, &m_compactDescriptorSet);
        dsai.pSetLayouts = &m_integrateDescriptorSetLayout;
        vkAllocateDescriptorSets(m_device, &dsai, &m_integrateDescriptorSet);

        // Compute command pool + buffer + fence
        VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};

        cpci.queueFamilyIndex = m_computeFamily;
        vkCreateCommandPool(m_device, &cpci, nullptr, &m_computeCommandPool);
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        cbai.commandPool = m_computeCommandPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_device, &cbai, &m_computeCommandBuffer);
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_device, &fci, nullptr, &m_computeFence);
        qDebug() << "[RTVK] Compute descriptor sets + command pool ready";
    }

    // -- create compute buffers ---------------------------------------------------
    void GranularVulkanWindow::createComputeBuffers()
    {
        if (m_gpuParticleCount == 0)
            return;
        VkDeviceSize particleBufSize = m_gpuParticleCount * sizeof(GpuParticle);
        createBuffer(m_device, m_physicalDevice, particleBufSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_gpuParticleBuffer, m_gpuParticleMemory);
        createBuffer(m_device, m_physicalDevice, particleBufSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_gpuParticleStaging, m_gpuParticleStagingMemory);
        m_hashTableSize = nextPow2(m_gpuParticleCount * 2);
        VkDeviceSize htSize = m_hashTableSize * sizeof(CellEntry);
        createBuffer(m_device, m_physicalDevice, htSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_hashTableBuffer, m_hashTableMemory);
        VkDeviceSize pcSize = m_gpuParticleCount * sizeof(ParticleCell);
        createBuffer(m_device, m_physicalDevice, pcSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_particleCellBuffer, m_particleCellMemory);
        createBuffer(m_device, m_physicalDevice, htSize + sizeof(uint32_t),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_cellOffsetBuffer, m_cellOffsetMemory);

        // Sorted particle output (same size as particleCellBuf)
        createBuffer(m_device, m_physicalDevice, pcSize,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_sortedParticleBuffer, m_sortedParticleMemory);

        // Block sums buffer (NUM_BLOCKS uint32s)
        uint32_t numBlocks = (m_hashTableSize + 511) / 512;
        createBuffer(m_device, m_physicalDevice, numBlocks * sizeof(uint32_t),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     m_blockSumsBuffer, m_blockSumsMemory);

        // Descriptor writes moved to initGpuSimulation (after descriptor sets created)
        qDebug() << "[RTVK] Compute buffers created:" << m_gpuParticleCount << "particles,"
                 << m_hashTableSize << "hash table";
    }

    // -- upload initial particle data to GPU --------------------------------------
    void GranularVulkanWindow::uploadParticleData(const std::vector<GpuParticle> &data)
    {
        if (data.empty() || !m_gpuParticleStaging)
            return;
        size_t byteSize = data.size() * sizeof(GpuParticle);
        void *mapped;
        vkMapMemory(m_device, m_gpuParticleStagingMemory, 0, byteSize, 0, &mapped);
        memcpy(mapped, data.data(), byteSize);
        vkUnmapMemory(m_device, m_gpuParticleStagingMemory);
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        cbai.commandPool = m_computeCommandPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(m_device, &cbai, &cb);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy copyRegion{};

        copyRegion.size = byteSize;
        vkCmdCopyBuffer(cb, m_gpuParticleStaging, m_gpuParticleBuffer, 1, &copyRegion);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        vkQueueSubmit(m_computeQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_computeQueue);
        vkFreeCommandBuffers(m_device, m_computeCommandPool, 1, &cb);
        qDebug() << "[RTVK] Uploaded" << data.size() << "particles to GPU";
    }

    // -- initialize GPU simulation ------------------------------------------------
    void GranularVulkanWindow::initGpuSimulation(
        uint32_t particleCount, float particleRadius, const SimParams &params)
    {
        vkDeviceWaitIdle(m_device);
        cleanupComputeResources();
        m_gpuParticleCount = particleCount;
        m_cellSize = particleRadius * 2.0f;
        m_simParams = params;
        createComputeBuffers();
        createComputePipelines();
        createComputeDescriptorSets();

        // Descriptor writes — must be AFTER descriptor sets are created
        VkDeviceSize particleBufSize = m_gpuParticleCount * sizeof(GpuParticle);
        VkDeviceSize htSize = m_hashTableSize * sizeof(CellEntry);
        VkDeviceSize pcSize = m_gpuParticleCount * sizeof(ParticleCell);
        uint32_t numBlocks = (m_hashTableSize + 511) / 512;
        VkDescriptorBufferInfo particleInfo{m_gpuParticleBuffer, 0, particleBufSize};
        VkDescriptorBufferInfo htInfo{m_hashTableBuffer, 0, htSize};
        VkDescriptorBufferInfo pcInfo{m_particleCellBuffer, 0, pcSize};
        VkDescriptorBufferInfo coInfo{m_cellOffsetBuffer, 0, htSize + sizeof(uint32_t)};
        VkDescriptorBufferInfo sortedInfo{m_sortedParticleBuffer, 0, pcSize};
        VkDescriptorBufferInfo blockSumsInfo{m_blockSumsBuffer, 0, numBlocks * sizeof(uint32_t)};

        // spatial_hash (0=particles, 1=hashTable, 2=unsortedCells, 3=slots)
        {
            VkDescriptorBufferInfo *infos[4] = {&particleInfo, &htInfo, &pcInfo, &coInfo};
            VkWriteDescriptorSet writes[4]{};
            for (int i = 0; i < 4; ++i)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = m_spatialHashDescriptorSet;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = infos[i];
            }
            vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);
        }

        // constraint (0=particles, 1=hashTable, 2=sortedList)
        {
            VkDescriptorBufferInfo *infos[3] = {&particleInfo, &htInfo, &sortedInfo};
            VkWriteDescriptorSet writes[3]{};
            for (int i = 0; i < 3; ++i)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = m_constraintDescriptorSet;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = infos[i];
            }
            vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
        }

        // compact (0=hashTable, 1=blockSums, 2=unsortedCells, 3=slots, 4=sortedOut)
        {
            VkDescriptorBufferInfo *infos[5] = {&htInfo, &blockSumsInfo, &pcInfo, &coInfo, &sortedInfo};
            VkWriteDescriptorSet writes[5]{};
            for (int i = 0; i < 5; ++i)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = m_compactDescriptorSet;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = infos[i];
            }
            vkUpdateDescriptorSets(m_device, 5, writes, 0, nullptr);
        }

        // integrate (0=particles)
        {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = m_integrateDescriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &particleInfo;
            vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        }

        m_computeReady = true;
        qDebug() << "[RTVK] initGpuSimulation: full, N=" << m_gpuParticleCount;
    }

    // -- GPU simulation step ------------------------------------------------------
    void GranularVulkanWindow::gpuSimStep(float deltaTime)
    {
        if (!m_computeReady || m_gpuParticleCount == 0)
            return;
        float dt = std::min(deltaTime, 0.033f);
        uint32_t workgroupCount = (m_gpuParticleCount + 255) / 256;
        vkWaitForFences(m_device, 1, &m_computeFence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_device, 1, &m_computeFence);
        VkCommandBuffer cb = m_computeCommandBuffer;
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);

        // Step 1: Integrate (forces + predict positions)
        {
            struct IntegratePC
            {
                float dt;
                float _pad0[3];
                float gravX, gravY, gravZ;
                float damping;
                uint32_t mode;
            } pc{};

            pc.dt = dt;
            pc.gravX = m_simParams.gravity.x;
            pc.gravY = m_simParams.gravity.y;
            pc.gravZ = m_simParams.gravity.z;
            pc.damping = 0.02f;
            pc.mode = 0;
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_integratePipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_integratePipelineLayout, 0, 1, &m_integrateDescriptorSet, 0, nullptr);
            vkCmdPushConstants(cb, m_integratePipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IntegratePC), &pc);
            vkCmdDispatch(cb, workgroupCount, 1, 1);
        }

        // Barrier: integrate write -> spatial_hash read
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // Zero out hash table and cell offsets
        vkCmdFillBuffer(cb, m_hashTableBuffer, 0, m_hashTableSize * sizeof(CellEntry), 0);
        vkCmdFillBuffer(cb, m_cellOffsetBuffer, 0, (m_hashTableSize + 1) * sizeof(uint32_t), 0);

        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // Step 2: Spatial hash
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_spatialHashPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_spatialHashPipelineLayout, 0, 1, &m_spatialHashDescriptorSet, 0, nullptr);
        vkCmdDispatch(cb, workgroupCount, 1, 1);

        // Barrier: spatial_hash write -> compact read
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // Step 2.5: Compact (prefix sum + scatter)
        {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_compactPipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_compactPipelineLayout, 0, 1, &m_compactDescriptorSet, 0, nullptr);
            uint32_t numBlocks = (m_hashTableSize + 511) / 512;

            // Phase 0: Per-block exclusive prefix sum
            uint32_t phase = 0;
            vkCmdPushConstants(cb, m_compactPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &phase);
            vkCmdDispatch(cb, numBlocks, 1, 1);

            {
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            // Phase 1: Block sum scan (single workgroup)
            phase = 1;
            vkCmdPushConstants(cb, m_compactPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &phase);
            vkCmdDispatch(cb, 1, 1, 1);

            {
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            // Phase 2: Add block offsets
            phase = 2;
            vkCmdPushConstants(cb, m_compactPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &phase);
            vkCmdDispatch(cb, numBlocks, 1, 1);

            {
                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            // Phase 3: Scatter particles to sorted list
            phase = 3;
            vkCmdPushConstants(cb, m_compactPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &phase);
            vkCmdDispatch(cb, workgroupCount, 1, 1);
        }

        // Barrier: compact write -> constraint read
        {

            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // Step 3: Constraint projection (multiple iterations)
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_constraintPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_constraintPipelineLayout, 0, 1, &m_constraintDescriptorSet, 0, nullptr);
        uint32_t constraintIters = m_simParams.constraintIterations;
        for (uint32_t iter = 0; iter < constraintIters; ++iter)
        {

            vkCmdDispatch(cb, workgroupCount, 1, 1);
            if (iter < constraintIters - 1)

            {

                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

                barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cb,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     0, 1, &barrier, 0, nullptr, 0, nullptr);
            }
        }

        // Step 4: Update velocities + commit positions
        {
            struct IntegratePC
            {
                float dt;
                float _pad0[3];
                float gravX, gravY, gravZ;
                float damping;
                uint32_t mode;
            } pc{};
            pc.dt = dt;
            pc.mode = 1; // velocity update
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_integratePipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_integratePipelineLayout, 0, 1, &m_integrateDescriptorSet, 0, nullptr);
            vkCmdPushConstants(cb, m_integratePipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(IntegratePC), &pc);
            vkCmdDispatch(cb, workgroupCount, 1, 1);
        }

        // Barrier: integrate write -> vertex read (for rendering)
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        // Barrier: compute write -> vertex read (for rendering)
        {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            vkCmdPipelineBarrier(cb,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        vkQueueSubmit(m_computeQueue, 1, &si, m_computeFence);
    }

    // -- read back particle positions (for CPU-side queries) ----------------------
    std::vector<glm::vec3> GranularVulkanWindow::gpuSimGetPositions()
    {
        std::vector<glm::vec3> positions;
        if (!m_computeReady || m_gpuParticleCount == 0)
            return positions;
        vkWaitForFences(m_device, 1, &m_computeFence, VK_TRUE, UINT64_MAX);
        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};

        cbai.commandPool = m_computeCommandPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(m_device, &cbai, &cb);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy copy{};

        copy.size = m_gpuParticleCount * sizeof(GpuParticle);
        vkCmdCopyBuffer(cb, m_gpuParticleBuffer, m_gpuParticleStaging, 1, &copy);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        vkQueueSubmit(m_computeQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_computeQueue);
        vkFreeCommandBuffers(m_device, m_computeCommandPool, 1, &cb);
        void *mapped;
        vkMapMemory(m_device, m_gpuParticleStagingMemory, 0, copy.size, 0, &mapped);
        GpuParticle *particles = static_cast<GpuParticle *>(mapped);
        positions.reserve(m_gpuParticleCount);
        for (uint32_t i = 0; i < m_gpuParticleCount; ++i)
        {
            positions.emplace_back(
                particles[i].posX, particles[i].posY, particles[i].posZ);
        }
        vkUnmapMemory(m_device, m_gpuParticleStagingMemory);
        return positions;
    }

    // -- cleanup compute resources ------------------------------------------------
    void GranularVulkanWindow::cleanupComputeResources()
    {

        if (!m_device)
            return;
        if (m_computeFence)
        {
            vkDestroyFence(m_device, m_computeFence, nullptr);
            m_computeFence = VK_NULL_HANDLE;
        }

        if (m_computeCommandPool)
        {
            vkDestroyCommandPool(m_device, m_computeCommandPool, nullptr);
            m_computeCommandPool = VK_NULL_HANDLE;
        }

        if (m_computeDescriptorPool)
        {
            vkDestroyDescriptorPool(m_device, m_computeDescriptorPool, nullptr);
            m_computeDescriptorPool = VK_NULL_HANDLE;
        }

        if (m_spatialHashDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(m_device, m_spatialHashDescriptorSetLayout, nullptr);
            m_spatialHashDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (m_constraintDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(m_device, m_constraintDescriptorSetLayout, nullptr);
            m_constraintDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (m_integrateDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(m_device, m_integrateDescriptorSetLayout, nullptr);
            m_integrateDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (m_spatialHashPipelineLayout)
        {
            vkDestroyPipelineLayout(m_device, m_spatialHashPipelineLayout, nullptr);
            m_spatialHashPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_constraintPipelineLayout)
        {
            vkDestroyPipelineLayout(m_device, m_constraintPipelineLayout, nullptr);
            m_constraintPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_integratePipelineLayout)
        {
            vkDestroyPipelineLayout(m_device, m_integratePipelineLayout, nullptr);
            m_integratePipelineLayout = VK_NULL_HANDLE;
        }

        if (m_spatialHashPipeline)
        {
            vkDestroyPipeline(m_device, m_spatialHashPipeline, nullptr);
            m_spatialHashPipeline = VK_NULL_HANDLE;
        }

        if (m_constraintPipeline)
        {
            vkDestroyPipeline(m_device, m_constraintPipeline, nullptr);
            m_constraintPipeline = VK_NULL_HANDLE;
        }

        if (m_integratePipeline)
        {
            vkDestroyPipeline(m_device, m_integratePipeline, nullptr);
            m_integratePipeline = VK_NULL_HANDLE;
        }

        if (m_gpuParticleBuffer)
        {
            vkDestroyBuffer(m_device, m_gpuParticleBuffer, nullptr);
            vkFreeMemory(m_device, m_gpuParticleMemory, nullptr);
            m_gpuParticleBuffer = VK_NULL_HANDLE;
            m_gpuParticleMemory = VK_NULL_HANDLE;
        }

        if (m_gpuParticleStaging)
        {
            vkDestroyBuffer(m_device, m_gpuParticleStaging, nullptr);
            vkFreeMemory(m_device, m_gpuParticleStagingMemory, nullptr);
            m_gpuParticleStaging = VK_NULL_HANDLE;
            m_gpuParticleStagingMemory = VK_NULL_HANDLE;
        }

        if (m_hashTableBuffer)
        {
            vkDestroyBuffer(m_device, m_hashTableBuffer, nullptr);
            vkFreeMemory(m_device, m_hashTableMemory, nullptr);
            m_hashTableBuffer = VK_NULL_HANDLE;
            m_hashTableMemory = VK_NULL_HANDLE;
        }

        if (m_particleCellBuffer)
        {
            vkDestroyBuffer(m_device, m_particleCellBuffer, nullptr);
            vkFreeMemory(m_device, m_particleCellMemory, nullptr);
            m_particleCellBuffer = VK_NULL_HANDLE;
            m_particleCellMemory = VK_NULL_HANDLE;
        }

        if (m_cellOffsetBuffer)
        {
            vkDestroyBuffer(m_device, m_cellOffsetBuffer, nullptr);
            vkFreeMemory(m_device, m_cellOffsetMemory, nullptr);
            m_cellOffsetBuffer = VK_NULL_HANDLE;
            m_cellOffsetMemory = VK_NULL_HANDLE;
        }

        m_computeReady = false;
        m_gpuParticleCount = 0;
        m_hashTableSize = 0;
        m_spatialHashDescriptorSet = VK_NULL_HANDLE;
        m_compactDescriptorSet = VK_NULL_HANDLE;
        m_constraintDescriptorSet = VK_NULL_HANDLE;
        m_integrateDescriptorSet = VK_NULL_HANDLE;
    }

    // -- dispatch spatial hash (inline, called by gpuSimStep) --------------------
    void GranularVulkanWindow::dispatchSpatialHash(VkCommandBuffer) {}

    // -- dispatch constraint projection (inline, called by gpuSimStep) -----------
    void GranularVulkanWindow::dispatchConstraintProjection(VkCommandBuffer, uint32_t) {}

    // -- dispatch integration (inline, called by gpuSimStep) ----------------------
    void GranularVulkanWindow::dispatchIntegration(VkCommandBuffer, float) {}

} // namespace rtvk::render
