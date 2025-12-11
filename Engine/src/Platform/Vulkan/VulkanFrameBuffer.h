#pragma once

#include "Engine/Renderer/Framebuffer.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace Engine {

    class VulkanFramebuffer : public Framebuffer
    {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec);
        virtual ~VulkanFramebuffer();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        // 【关键】返回 ImGui 所需的 DescriptorSet 句柄
        virtual uint64_t GetColorAttachmentRendererID(uint32_t index = 0) const override;

        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }
        virtual uint32_t GetWidth() const override { return m_Specification.Width; }
        virtual uint32_t GetHeight() const const override { return m_Specification.Height; }

    private:
        void Invalidate(); // 负责创建所有的 Vulkan 资源
        void Release();    // 负责销毁资源

    private:
        FramebufferSpecification m_Specification;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE; // FBO 必须绑定 RenderPass
        VkFramebuffer m_VulkanFramebuffer = VK_NULL_HANDLE;

        // 颜色附件资源
        VkImage m_ColorImage = VK_NULL_HANDLE;
        VkImageView m_ColorImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_ColorImageMemory = VK_NULL_HANDLE;
        VkSampler m_ColorSampler = VK_NULL_HANDLE;

        // 深度附件资源 (可选)
        VkImage m_DepthImage = VK_NULL_HANDLE;
        VkImageView m_DepthImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;

        // 【核心】供 ImGui 使用的 Descriptor Set
        // 每一帧都会用这个 Descriptor Set 来画 Image
        VkDescriptorSet m_MaterialDescriptorSet = VK_NULL_HANDLE;
    };

}