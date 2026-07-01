#include <Poseidon/Graphics/GraphicsEngineFactory.hpp>
#include <Poseidon/Graphics/Core/EngineFactory.hpp>
#include <Poseidon/Graphics/Shared/WindowMetrics.hpp>
#include <PoseidonVK/EngineVK.hpp>

using Poseidon::Engine;
using Poseidon::GraphicsBackendDescriptor;
using Poseidon::GraphicsEngineParams;
using Poseidon::GraphicsEngineFactory;
using Poseidon::CreateEngineVK;

namespace
{
Engine* CreateVKBackend(const GraphicsEngineParams& params)
{
    return CreateEngineVK(params.width, params.height, params.useWindow, params.bitsPerPixel);
}

bool IsVKAvailable()
{
    // vulkan availability check could eventually query vkEnumerateInstanceExtensionProperties
    return true;
}
} // namespace

namespace Poseidon
{
void RegisterVulkanGraphicsBackend()
{
    GraphicsEngineFactory::Register(GraphicsBackendDescriptor{
        "vulkan",
        "Vulkan 1.1+ (SDL3)",
        300, // higher priority than gles32 on android
        &CreateVKBackend,
        &IsVKAvailable,
    });
}
} // namespace Poseidon
