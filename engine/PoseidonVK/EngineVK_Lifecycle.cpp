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
#ifndef NDEBUG
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    std::vector<const char*> layers;
#ifndef NDEBUG
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
#endif

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

#ifndef NDEBUG
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
#endif

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

    // pick format: prefer BGRA8 SRGB
    VkSurfaceFormatKHR fmt = ss.formats[0];
    for (auto& f : ss.formats)
    {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            fmt = f;
            break;
        }
    }

    // pick present mode: prefer mailbox (triple buffering), fallback to fifo
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& m : ss.presentModes)
    {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            mode = m;
            break;
        }
    }

    // pick extent
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
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

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

    // create image views
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
    return true;
}

bool EngineVK::CreateRenderPass()
{
    VkAttachmentDescription colorAtt{};
    colorAtt.format = _swapchainFormat;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &colorAtt;
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
        VkImageView attachments[] = { _swapchainImageViews[i] };

        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = _renderPass;
        fbci.attachmentCount = 1;
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
    _renderFinishedSem.resize(MAX_FRAMES_IN_FLIGHT);
    _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(_device, &sci, nullptr, &_imageAvailableSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(_device, &sci, nullptr, &_renderFinishedSem[i]) != VK_SUCCESS ||
            vkCreateFence(_device, &fci, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
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

    if (!CreateVkInstance()) return;
    if (!CreateVkSurface()) return;
    if (!PickPhysicalDevice()) return;
    if (!CreateLogicalDevice()) return;
    if (!CreateSwapchain()) return;
    if (!CreateRenderPass()) return;
    if (!CreateFramebuffers()) return;
    if (!CreateCommandPool()) return;
    if (!CreateSyncObjects()) return;

    _vkReady = true;
    LOG_INFO(Graphics, "PoseidonVK: vulkan pipeline fully initialized");
}

void EngineVK::ShutdownVulkan()
{
    if (_device)
        vkDeviceWaitIdle(_device);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (_inFlightFences[i]) vkDestroyFence(_device, _inFlightFences[i], nullptr);
        if (_renderFinishedSem[i]) vkDestroySemaphore(_device, _renderFinishedSem[i], nullptr);
        if (_imageAvailableSem[i]) vkDestroySemaphore(_device, _imageAvailableSem[i], nullptr);
    }

    if (_commandPool) vkDestroyCommandPool(_device, _commandPool, nullptr);

    for (auto fb : _swapchainFramebuffers)
        vkDestroyFramebuffer(_device, fb, nullptr);

    if (_renderPass) vkDestroyRenderPass(_device, _renderPass, nullptr);

    for (auto iv : _swapchainImageViews)
        vkDestroyImageView(_device, iv, nullptr);

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
