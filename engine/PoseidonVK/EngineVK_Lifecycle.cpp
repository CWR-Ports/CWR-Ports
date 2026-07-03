/*
 * PoseidonVK Vulkan lifecycle management
 *
 * handles vulkan instance creation, physical and logical device selection,
 * swapchain setup, render pass and framebuffer creation, command pool
 * allocation, and synchronization primitives. this is the vulkan equivalent
 * of what the gles32 constructor does with sdl and opengl context init.
 */

#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Dev/Debug/DebugOverlay.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <algorithm>
#include <cstring>

namespace Poseidon
{

static VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR(Graphics, "VK: {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN(Graphics, "VK: {}", data->pMessage);
    else
        LOG_DEBUG(Graphics, "VK: {}", data->pMessage);
    return VK_FALSE;
}

struct QueueFamilyIndices
{
    int graphics = -1;
    int present = -1;
    bool complete() const { return graphics >= 0 && present >= 0; }
};

static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++)
    {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphics = (int)i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport)
            indices.present = (int)i;

        if (indices.complete())
            break;
    }
    return indices;
}

struct SwapchainSupport
{
    VkSurfaceCapabilitiesKHR caps;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

static SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapchainSupport s;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &s.caps);

    uint32_t fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &fc, nullptr);
    s.formats.resize(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &fc, s.formats.data());

    uint32_t pc = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &pc, nullptr);
    s.presentModes.resize(pc);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &pc, s.presentModes.data());
    return s;
}

bool EngineVK::CreateVkInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "PoseidonVK";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Poseidon";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    uint32_t sdlExtCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);

    std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> available(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, available.data());
    for (auto& l : available)
    {
        if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            break;
        }
    }

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();

    if (vkCreateInstance(&ci, nullptr, &_instance) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vkCreateInstance failed");
        return false;
    }

    auto createMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(_instance, "vkCreateDebugUtilsMessengerEXT");
    if (createMessenger)
    {
        VkDebugUtilsMessengerCreateInfoEXT dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = VkDebugCallback;
        createMessenger(_instance, &dci, nullptr, &_debugMessenger);
    }

    LOG_INFO(Graphics, "PoseidonVK: Vulkan instance created ({} extensions, {} layers)",
             extensions.size(), layers.size());
    return true;
}

bool EngineVK::CreateVkSurface()
{
    if (!SDL_Vulkan_CreateSurface(_sdlWindow, _instance, nullptr, &_surface))
    {
        LOG_ERROR(Graphics, "PoseidonVK: SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

bool EngineVK::PickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(_instance, &count, nullptr);
    if (count == 0)
    {
        LOG_ERROR(Graphics, "PoseidonVK: no vulkan capable gpu found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(_instance, &count, devices.data());

    for (auto& dev : devices)
    {
        QueueFamilyIndices idx = FindQueueFamilies(dev, _surface);
        if (!idx.complete())
            continue;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());

        bool hasSwapchain = false;
        for (auto& e : exts)
        {
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            {
                hasSwapchain = true;
                break;
            }
        }
        if (!hasSwapchain)
            continue;

        SwapchainSupport ss = QuerySwapchainSupport(dev, _surface);
        if (ss.formats.empty() || ss.presentModes.empty())
            continue;

        _physicalDevice = dev;
        _queueFamilyIndices[0] = idx.graphics;
        _queueFamilyIndices[1] = idx.present;

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        LOG_INFO(Graphics, "PoseidonVK: selected gpu: {}", props.deviceName);
        return true;
    }

    LOG_ERROR(Graphics, "PoseidonVK: no suitable gpu found");
    return false;
}

bool EngineVK::CreateLogicalDevice()
{
    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCis;

    int uniqueFamilies[] = { _queueFamilyIndices[0], _queueFamilyIndices[1] };
    int uniqueCount = (uniqueFamilies[0] == uniqueFamilies[1]) ? 1 : 2;

    for (int i = 0; i < uniqueCount; i++)
    {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = (uint32_t)uniqueFamilies[i];
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        queueCis.push_back(qci);
    }

    VkPhysicalDeviceFeatures features{};

    const char* deviceExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (uint32_t)queueCis.size();
    dci.pQueueCreateInfos = queueCis.data();
    dci.pEnabledFeatures = &features;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExts;

    if (vkCreateDevice(_physicalDevice, &dci, nullptr, &_device) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vkCreateDevice failed");
        return false;
    }

    vkGetDeviceQueue(_device, (uint32_t)_queueFamilyIndices[0], 0, &_graphicsQueue);
    vkGetDeviceQueue(_device, (uint32_t)_queueFamilyIndices[1], 0, &_presentQueue);

    LOG_INFO(Graphics, "PoseidonVK: logical device created (graphics={}, present={})",
             _queueFamilyIndices[0], _queueFamilyIndices[1]);
    return true;
}

bool EngineVK::CreateSwapchain()
{
    SwapchainSupport ss = QuerySwapchainSupport(_physicalDevice, _surface);

    VkSurfaceFormatKHR fmt = ss.formats[0];
    for (auto& f : ss.formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            fmt = f;
            break;
        }
    }

    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& m : ss.presentModes)
    {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            mode = m;
            break;
        }
    }

    VkExtent2D extent;
    if (ss.caps.currentExtent.width != UINT32_MAX)
    {
        extent = ss.caps.currentExtent;
    }
    else
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(_sdlWindow, &pw, &ph);
        extent.width = std::max(ss.caps.minImageExtent.width,
                       std::min(ss.caps.maxImageExtent.width, (uint32_t)pw));
        extent.height = std::max(ss.caps.minImageExtent.height,
                        std::min(ss.caps.maxImageExtent.height, (uint32_t)ph));
    }

    uint32_t imageCount = ss.caps.minImageCount + 1;
    if (ss.caps.maxImageCount > 0 && imageCount > ss.caps.maxImageCount)
        imageCount = ss.caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = _surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    uint32_t familyIndices[] = { (uint32_t)_queueFamilyIndices[0], (uint32_t)_queueFamilyIndices[1] };
    if (_queueFamilyIndices[0] != _queueFamilyIndices[1])
    {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = familyIndices;
    }
    else
    {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    sci.preTransform = ss.caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(_device, &sci, nullptr, &_swapchain) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vkCreateSwapchainKHR failed");
        return false;
    }

    vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, nullptr);
    _swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, _swapchainImages.data());

    _swapchainFormat = fmt.format;
    _swapchainExtent = extent;

    _swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = _swapchainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = _swapchainFormat;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(_device, &ivci, nullptr, &_swapchainImageViews[i]) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "PoseidonVK: vkCreateImageView failed for swapchain image {}", i);
            return false;
        }
    }

    _w = (int)extent.width;
    _h = (int)extent.height;
    _maxGuardX = _w;
    _maxGuardY = _h;

    LOG_INFO(Graphics, "PoseidonVK: swapchain created {}x{} ({} images, format {})",
             _w, _h, imageCount, (int)_swapchainFormat);
    VkImageCreateInfo dimg{};
    dimg.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    dimg.imageType = VK_IMAGE_TYPE_2D;
    dimg.extent.width = _swapchainExtent.width;
    dimg.extent.height = _swapchainExtent.height;
    dimg.extent.depth = 1;
    dimg.mipLevels = 1;
    dimg.arrayLayers = 1;
    dimg.format = _depthFormat;
    dimg.tiling = VK_IMAGE_TILING_OPTIMAL;
    dimg.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dimg.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dimg.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo dalloc{};
    dalloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(_vmaAllocator, &dimg, &dalloc, &_depthImage, &_depthImageAllocation, nullptr) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: failed to create depth image");
        return false;
    }

    VkImageViewCreateInfo dview{};
    dview.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dview.image = _depthImage;
    dview.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dview.format = _depthFormat;
    dview.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dview.subresourceRange.baseMipLevel = 0;
    dview.subresourceRange.levelCount = 1;
    dview.subresourceRange.baseArrayLayer = 0;
    dview.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &dview, nullptr, &_depthImageView) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: failed to create depth image view");
        return false;
    }

    return true;
}

bool EngineVK::CreateRenderPass()
{
    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = _swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = _depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = attachments;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    if (vkCreateRenderPass(_device, &rpci, nullptr, &_renderPass) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vkCreateRenderPass failed");
        return false;
    }
    return true;
}

bool EngineVK::CreateFramebuffers()
{
    _swapchainFramebuffers.resize(_swapchainImageViews.size());
    for (size_t i = 0; i < _swapchainImageViews.size(); i++)
    {
        VkImageView attachments[] = { _swapchainImageViews[i], _depthImageView };

        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = _renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width = _swapchainExtent.width;
        fbci.height = _swapchainExtent.height;
        fbci.layers = 1;

        if (vkCreateFramebuffer(_device, &fbci, nullptr, &_swapchainFramebuffers[i]) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "PoseidonVK: vkCreateFramebuffer failed for image {}", i);
            return false;
        }
    }
    return true;
}

bool EngineVK::CreateCommandPool()
{
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = (uint32_t)_queueFamilyIndices[0];

    if (vkCreateCommandPool(_device, &cpci, nullptr, &_commandPool) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vkCreateCommandPool failed");
        return false;
    }

    _commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = _commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = (uint32_t)_commandBuffers.size();

    if (vkAllocateCommandBuffers(_device, &cbai, _commandBuffers.data()) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vkAllocateCommandBuffers failed");
        return false;
    }
    return true;
}

bool EngineVK::CreateSyncObjects()
{
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    _imageAvailableSem.resize(MAX_FRAMES_IN_FLIGHT);
    _renderFinishedSem.resize(_swapchainImages.size());
    _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    _imagesInFlight.assign(_swapchainImages.size(), VK_NULL_HANDLE);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(_device, &sci, nullptr, &_imageAvailableSem[i]) != VK_SUCCESS ||
            vkCreateFence(_device, &fci, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "PoseidonVK: sync object creation failed");
            return false;
        }
    }

    for (size_t i = 0; i < _swapchainImages.size(); i++)
    {
        if (vkCreateSemaphore(_device, &sci, nullptr, &_renderFinishedSem[i]) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "PoseidonVK: sync object creation failed");
            return false;
        }
    }
    return true;
}

void EngineVK::InitVulkan()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        LOG_ERROR(Graphics, "PoseidonVK: SDL_Init failed: {}", SDL_GetError());
        return;
    }

    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    _sdlWindow = SDL_CreateWindow("Poseidon [Vulkan]", _w, _h, flags);
    if (!_sdlWindow)
    {
        LOG_ERROR(Graphics, "PoseidonVK: SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    _eventWindow.Attach(_sdlWindow, _w, _h); // Link backend window listener to receive message states

    if (!CreateVkInstance()) return;
    if (!CreateVkSurface()) return;
    if (!PickPhysicalDevice()) return;
    if (!CreateLogicalDevice()) return;
    
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = _physicalDevice;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    if (vmaCreateAllocator(&allocatorInfo, &_vmaAllocator) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: vmaCreateAllocator failed");
        return;
    }

    if (!CreateSwapchain()) return;
    if (!CreateRenderPass()) return;
    if (!CreateFramebuffers()) return;
    if (!CreateCommandPool()) return;
    if (!CreateSyncObjects()) return;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkBufferCreateInfo vboInfo{};
        vboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vboInfo.size = MeshBufferLength * sizeof(TLVertex);
        vboInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo vboAllocInfo{};
        if (vmaCreateBuffer(_vmaAllocator, &vboInfo, &allocInfo, &_vbo[i], &_vboAllocation[i], &vboAllocInfo) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "PoseidonVK: Failed to create dynamic VBO for frame {}", i);
            return;
        }
        _vboMapped[i] = vboAllocInfo.pMappedData;

        VkBufferCreateInfo iboInfo{};
        iboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        iboInfo.size = IndexBufferLength * sizeof(WORD);
        iboInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        iboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationInfo iboAllocInfo{};
        if (vmaCreateBuffer(_vmaAllocator, &iboInfo, &allocInfo, &_ibo[i], &_iboAllocation[i], &iboAllocInfo) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "PoseidonVK: Failed to create dynamic IBO for frame {}", i);
            return;
        }
        _iboMapped[i] = iboAllocInfo.pMappedData;
    }

    VkImageCreateInfo whiteInfo{};
    whiteInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    whiteInfo.imageType = VK_IMAGE_TYPE_2D;
    whiteInfo.extent.width = 1;
    whiteInfo.extent.height = 1;
    whiteInfo.extent.depth = 1;
    whiteInfo.mipLevels = 1;
    whiteInfo.arrayLayers = 1;
    whiteInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    whiteInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    whiteInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    whiteInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    whiteInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    whiteInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo whiteAllocInfo{};
    whiteAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(_vmaAllocator, &whiteInfo, &whiteAllocInfo, &_fallbackWhiteImage, &_fallbackWhiteAllocation, nullptr) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: Failed to create fallback white image");
        return;
    }

    VkImageViewCreateInfo whiteViewInfo{};
    whiteViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    whiteViewInfo.image = _fallbackWhiteImage;
    whiteViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    whiteViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    whiteViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    whiteViewInfo.subresourceRange.baseMipLevel = 0;
    whiteViewInfo.subresourceRange.levelCount = 1;
    whiteViewInfo.subresourceRange.baseArrayLayer = 0;
    whiteViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &whiteViewInfo, nullptr, &_fallbackWhiteImageView) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: Failed to create fallback white image view");
        return;
    }

    VkImageViewCreateInfo whiteArrayViewInfo{};
    whiteArrayViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    whiteArrayViewInfo.image = _fallbackWhiteImage;
    whiteArrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    whiteArrayViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    whiteArrayViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    whiteArrayViewInfo.subresourceRange.baseMipLevel = 0;
    whiteArrayViewInfo.subresourceRange.levelCount = 1;
    whiteArrayViewInfo.subresourceRange.baseArrayLayer = 0;
    whiteArrayViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &whiteArrayViewInfo, nullptr, &_fallbackWhiteArrayImageView) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: Failed to create fallback white array image view");
        return;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 16.0f;

    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_defaultSampler) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: Failed to create default sampler");
        return;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;

    VkBufferCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stageInfo.size = 4;
    stageInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stageAllocInfo{};
    stageAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    stageAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stageResult{};
    if (vmaCreateBuffer(_vmaAllocator, &stageInfo, &stageAllocInfo, &stagingBuffer, &stagingAlloc, &stageResult) == VK_SUCCESS)
    {
        uint32_t whitePixel = 0xffffffff;
        std::memcpy(stageResult.pMappedData, &whitePixel, 4);

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandPool = _commandPool;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(_device, &cmdAlloc, &cb) == VK_SUCCESS)
        {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cb, &begin);

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = _fallbackWhiteImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {1, 1, 1};

            vkCmdCopyBufferToImage(cb, stagingBuffer, _fallbackWhiteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            vkEndCommandBuffer(cb);

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &cb;

            vkQueueSubmit(_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(_graphicsQueue);

            vkFreeCommandBuffers(_device, _commandPool, 1, &cb);
        }
        vmaDestroyBuffer(_vmaAllocator, stagingBuffer, stagingAlloc);
    }

    _vkReady = true;
    LOG_INFO(Graphics, "PoseidonVK: vulkan pipeline fully initialized");
}

void EngineVK::ShutdownVulkan()
{
    if (_device == VK_NULL_HANDLE)
    {
        if (_instance) vkDestroyInstance(_instance, nullptr);
        if (_sdlWindow) SDL_DestroyWindow(_sdlWindow);
        _sdlWindow = nullptr;
        _vkReady = false;
        return;
    }

    vkDeviceWaitIdle(_device);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (_inFlightFences[i]) vkDestroyFence(_device, _inFlightFences[i], nullptr);
        if (_imageAvailableSem[i]) vkDestroySemaphore(_device, _imageAvailableSem[i], nullptr);
    }

    for (size_t i = 0; i < _renderFinishedSem.size(); i++)
    {
        if (_renderFinishedSem[i]) vkDestroySemaphore(_device, _renderFinishedSem[i], nullptr);
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (_ibo[i])
        {
            vmaDestroyBuffer(_vmaAllocator, _ibo[i], _iboAllocation[i]);
            _ibo[i] = VK_NULL_HANDLE;
        }
        if (_vbo[i])
        {
            vmaDestroyBuffer(_vmaAllocator, _vbo[i], _vboAllocation[i]);
            _vbo[i] = VK_NULL_HANDLE;
        }
    }

    if (_fallbackWhiteImageView)
    {
        vkDestroyImageView(_device, _fallbackWhiteImageView, nullptr);
        _fallbackWhiteImageView = VK_NULL_HANDLE;
    }
    if (_fallbackWhiteArrayImageView)
    {
        vkDestroyImageView(_device, _fallbackWhiteArrayImageView, nullptr);
        _fallbackWhiteArrayImageView = VK_NULL_HANDLE;
    }
    if (_fallbackWhiteImage)
    {
        vmaDestroyImage(_vmaAllocator, _fallbackWhiteImage, _fallbackWhiteAllocation);
        _fallbackWhiteImage = VK_NULL_HANDLE;
    }
    if (_defaultSampler)
    {
        vkDestroySampler(_device, _defaultSampler, nullptr);
        _defaultSampler = VK_NULL_HANDLE;
    }

    if (_commandPool) vkDestroyCommandPool(_device, _commandPool, nullptr);

    for (auto fb : _swapchainFramebuffers)
        vkDestroyFramebuffer(_device, fb, nullptr);

    if (_renderPass) vkDestroyRenderPass(_device, _renderPass, nullptr);

    for (auto iv : _swapchainImageViews)
        vkDestroyImageView(_device, iv, nullptr);

    if (_shadowSolidPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(_device, _shadowSolidPipeline, nullptr);
        _shadowSolidPipeline = VK_NULL_HANDLE;
    }
    if (_shadowPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(_device, _shadowPipelineLayout, nullptr);
        _shadowPipelineLayout = VK_NULL_HANDLE;
    }
    if (_shadowSolidVertexShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(_device, _shadowSolidVertexShader, nullptr);
        _shadowSolidVertexShader = VK_NULL_HANDLE;
    }
    if (_shadowSolidFragmentShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(_device, _shadowSolidFragmentShader, nullptr);
        _shadowSolidFragmentShader = VK_NULL_HANDLE;
    }
    _shadowPipelineRenderPass = VK_NULL_HANDLE;

    if (!_shadowLayerViews.empty())
    {
        for (auto view : _shadowLayerViews)
            vkDestroyImageView(_device, view, nullptr);
        _shadowLayerViews.clear();
    }
    if (!_shadowFramebuffers.empty())
    {
        for (auto fb : _shadowFramebuffers)
            vkDestroyFramebuffer(_device, fb, nullptr);
        _shadowFramebuffers.clear();
    }
    if (_shadowImageView != VK_NULL_HANDLE)
        vkDestroyImageView(_device, _shadowImageView, nullptr);
    if (_shadowImage != VK_NULL_HANDLE)
        vmaDestroyImage(_vmaAllocator, _shadowImage, _shadowImageAlloc);
    if (_shadowRenderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(_device, _shadowRenderPass, nullptr);
    if (_shadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(_device, _shadowSampler, nullptr);

    if (_vmaAllocator)
    {
        vmaDestroyAllocator(_vmaAllocator);
        _vmaAllocator = VK_NULL_HANDLE;
    }

    if (_swapchain) vkDestroySwapchainKHR(_device, _swapchain, nullptr);
    if (_device) vkDestroyDevice(_device, nullptr);
    if (_surface) vkDestroySurfaceKHR(_instance, _surface, nullptr);

#ifndef NDEBUG
    if (_debugMessenger)
    {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy)
            destroy(_instance, _debugMessenger, nullptr);
    }
#endif

    if (_instance) vkDestroyInstance(_instance, nullptr);

    if (_sdlWindow) SDL_DestroyWindow(_sdlWindow);
    _sdlWindow = nullptr;
    _vkReady = false;
}

} // namespace Poseidon