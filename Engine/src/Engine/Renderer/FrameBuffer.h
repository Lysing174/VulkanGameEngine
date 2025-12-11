#pragma once

#include "Engine/Core/Core.h" // 确保包含你的 Ref/SharedPtr 定义
#include <glm/glm.hpp>

namespace Engine {

    struct FramebufferSpecification
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        // 你可以根据需要添加更多附件格式，例如：
        // FramebufferFormat Format = FramebufferFormat::RGBA888; 

        uint32_t Samples = 1; // 多重采样，通常 1 用于编辑器 FBO
        bool SwapChainTarget = false; // 是否是主交换链的目标 (如果你用这个类管理主屏幕)
    };

    class Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        // 核心功能
        virtual void Bind() = 0;
        virtual void Unbind() = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // 【关键】获取纹理句柄，供 ImGui 使用 (必须返回一个 ImGui 能识别的 ID，在 Vulkan 中是 VkDescriptorSet*)
        // 这里使用 uint64_t 作为通用的 RendererID
        virtual uint64_t GetColorAttachmentRendererID(uint32_t index = 0) const = 0;

        // Getter
        virtual const FramebufferSpecification& GetSpecification() const = 0;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        // 静态工厂函数
        static std::shared_ptr<Framebuffer> Create(const FramebufferSpecification& spec);
    };

}