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

namespace rtvk::render {

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

    QTimer::singleShot(200, this, [this] { initVulkan(); });
}

void GranularVulkanWindow::resize()
{
    if (!m_hwnd || !m_container) return;
    RECT r;
    GetClientRect((HWND)m_container->winId(), &r);
    SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (m_vulkanReady) m_framebufferResized = true;
}

// ── update particle positions ────────────────────────────────
void GranularVulkanWindow::updateParticles(
    const std::vector<glm::vec3> &positions)
{
    if (!m_particlePipelineReady || positions.empty()) return;

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
        connect(t, &QTimer::timeout, this, [this] { drawFrame(); });
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
    if (!m_device) return;
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
    cleanupSwapchain();
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyDevice(m_device, nullptr);
    if (m_surface)
    {
        vkDestroySurfaceKHR(m_vulkanInstance.vkInstance(), m_surface, nullptr);
    }
    m_device = VK_NULL_HANDLE;
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
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
        if (isDeviceSuitable(d)) { m_physicalDevice = d; return; }
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
        if (ps) m_presentFamily = i;
        if (m_graphicsFamily != UINT32_MAX &&
            m_presentFamily != UINT32_MAX)
        {
            break;
        }
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
    std::set<uint32_t> uf{m_graphicsFamily, m_presentFamily};
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
}

// ── swapchain ────────────────────────────────────────────────
void GranularVulkanWindow::createSwapchain()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_physicalDevice, m_surface, &caps);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) { ext = {1280, 720}; }
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
    if (!m_vulkanReady) return;
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

    if (m_frameCallback) m_frameCallback(cb, ii);

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
    push.pointSize = 18.0f;  // pixels

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

} // namespace rtvk::render