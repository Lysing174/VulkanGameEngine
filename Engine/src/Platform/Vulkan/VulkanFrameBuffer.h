#pragma once

#include "Engine/Renderer/Framebuffer.h"
#include <vulkan/vulkan.h>

namespace Engine {

    class VulkanFramebuffer : public Framebuffer
    {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec);
        virtual ~VulkanFramebuffer();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        virtual uint64_t GetColorAttachmentRendererID(uint32_t index = 0) const override;

        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }
        virtual uint32_t GetWidth()  const override { return m_Specification.Width; }
        virtual uint32_t GetHeight() const override { return m_Specification.Height; }

        VkRenderPass   GetRenderPass()       const { return m_RenderPass; }
        VkFramebuffer  GetVulkanFramebuffer() const { return m_VulkanFramebuffer; }
        VkExtent2D     GetExtent()           const { return { m_Specification.Width, m_Specification.Height }; }
        bool           IsMSAA()              const { return m_Specification.Samples != 1; }
        VkImage        GetResolveImage()     const { return m_ColorResolveImage; }

        // 1x framebuffer / 1x 深度 resolve — 供高斯 splat pass (始终 1x) 使用
        VkFramebuffer  GetGaussianVulkanFramebuffer() const { return m_GaussianFramebuffer; }
        VkImage        GetDepthResolveImage()        const { return m_DepthResolveImage; }
        VkImageView    GetDepthResolveView()         const { return m_DepthResolveView; }

        // 高斯 pass 用的 1x 深度视图: MSAA 模式取 1x depth resolve, 否则取 1x depth (即 m_DepthMSAAView).
        // 高斯 pass 通过 b3(u_DepthMap) 采样它做遮挡.
        VkImageView    GetGaussianDepthView()        const { return IsMSAA() ? m_DepthResolveView : m_DepthMSAAView; }

    private:
        void Invalidate();
        void Release();
        void EnsureImGuiTexture();

        // Image helper
        void CreateImage(VkImageUsageFlags usage, VkSampleCountFlagBits samples,
                         VkFormat format, VkImage& image, VkDeviceMemory& memory);
        VkImageView CreateView(VkImage image, VkFormat format, VkImageAspectFlags aspect);
        void TransitionLayout(VkImage image, VkFormat format,
                              VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkImageAspectFlags aspectMask);

    private:
        FramebufferSpecification m_Specification;
        VkRenderPass  m_RenderPass         = VK_NULL_HANDLE;
        VkFramebuffer m_VulkanFramebuffer  = VK_NULL_HANDLE;
        VkFramebuffer m_GaussianFramebuffer = VK_NULL_HANDLE;  // 1x framebuffer for Gaussian splat pass

        // Formats
        VkFormat m_ColorFormat  = VK_FORMAT_B8G8R8A8_UNORM;
        VkFormat m_DepthFormat  = VK_FORMAT_D32_SFLOAT;

        // MSAA Color — attachment 0 (multisampled)
        VkImage         m_ColorMSAAImage    = VK_NULL_HANDLE;
        VkDeviceMemory  m_ColorMSAAMemory   = VK_NULL_HANDLE;
        VkImageView     m_ColorMSAAView     = VK_NULL_HANDLE;

        // Resolve Color — attachment 1 (single-sampled, displayed in ImGui)
        VkImage         m_ColorResolveImage   = VK_NULL_HANDLE;
        VkDeviceMemory  m_ColorResolveMemory  = VK_NULL_HANDLE;
        VkImageView     m_ColorResolveView    = VK_NULL_HANDLE;
        VkSampler       m_ColorSampler        = VK_NULL_HANDLE;

        // MSAA Depth — attachment 2 (multisampled)
        VkImage         m_DepthMSAAImage   = VK_NULL_HANDLE;
        VkDeviceMemory  m_DepthMSAAMemory  = VK_NULL_HANDLE;
        VkImageView     m_DepthMSAAView    = VK_NULL_HANDLE;

        // Resolve Depth — 1x (仅 MSAA 模式), mesh 深度 resolve 写入, 高斯读取做遮挡
        VkImage         m_DepthResolveImage   = VK_NULL_HANDLE;
        VkDeviceMemory  m_DepthResolveMemory  = VK_NULL_HANDLE;
        VkImageView     m_DepthResolveView    = VK_NULL_HANDLE;

        // ImGui descriptor set (from ImGui_ImplVulkan_AddTexture)
        mutable VkDescriptorSet m_ImGuiDescriptorSet     = VK_NULL_HANDLE;
        mutable bool            m_ImGuiTextureRegistered = false;
    };

}
