#include "pch.h"

#include "Framebuffer.h"
#include "Platform/Vulkan/VulkanFramebuffer.h" 

namespace Engine {

    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
    {
        // 这里根据你使用的渲染 API 决定实例化哪个子类
        // 假设你有一个 Renderer::GetAPI() 函数
        // switch (Renderer::GetAPI())
        // {
            // case RendererAPI::Vulkan:
        return std::make_shared<VulkanFramebuffer>(spec);
        // case RendererAPI::OpenGL: ...
    // }
    }
}