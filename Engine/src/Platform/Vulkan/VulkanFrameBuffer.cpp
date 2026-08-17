#include "pch.h"
#include "VulkanFrameBuffer.h"
#include "VulkanContext.h"
#include "backends/imgui_impl_vulkan.h"

namespace Engine {

    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec)
    {
        m_RenderPass = static_cast<VkRenderPass>(m_Specification.RenderPass);
        Invalidate();
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        Release();
    }

    void VulkanFramebuffer::Bind()   {}
    void VulkanFramebuffer::Unbind() {}

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        if (m_Specification.Width == width && m_Specification.Height == height) return;

        m_Specification.Width  = width;
        m_Specification.Height = height;
        Release();
        Invalidate();
    }

    // ========================================================================
    // Helpers
    // ========================================================================
    void VulkanFramebuffer::CreateImage(VkImageUsageFlags usage,
                                        VkSampleCountFlagBits samples,
                                        VkFormat format,
                                        VkImage& image,
                                        VkDeviceMemory& memory)
    {
        auto* context = VulkanContext::Get();
        auto device   = context->GetDevice();
        uint32_t w = m_Specification.Width;
        uint32_t h = m_Specification.Height;

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.extent        = { w, h, 1 };
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.format        = format;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage         = usage;
        imageInfo.samples       = samples;
        imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("VulkanFramebuffer: failed to create image!");

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device, image, &memReq);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = memReq.size;
        allocInfo.memoryTypeIndex = context->FindMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("VulkanFramebuffer: failed to allocate image memory!");

        vkBindImageMemory(device, image, memory, 0);
    }

    VkImageView VulkanFramebuffer::CreateView(VkImage image, VkFormat format,
                                               VkImageAspectFlags aspect)
    {
        auto device = VulkanContext::Get()->GetDevice();

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
            throw std::runtime_error("VulkanFramebuffer: failed to create image view!");

        return imageView;
    }

    void VulkanFramebuffer::TransitionLayout(VkImage image, VkFormat format,
                                              VkImageLayout oldLayout,
                                              VkImageLayout newLayout,
                                              VkImageAspectFlags aspectMask)
    {
        auto* context = VulkanContext::Get();
        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkImageMemoryBarrier barrier = {};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = oldLayout;
        barrier.newLayout                       = newLayout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = aspectMask;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        VkPipelineStageFlags srcStage, dstStage;
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        } else {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = 0;
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        context->EndSingleTimeCommands(cmd);
    }

    // ========================================================================
    // Invalidate — create all Vulkan resources
    // ========================================================================
    void VulkanFramebuffer::Invalidate()
    {
        auto* context = VulkanContext::Get();
        auto  device  = context->GetDevice();
        auto  msaaSamples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);
        bool  useMSAA = (m_Specification.Samples != 1);

        m_ColorFormat = context->GetSwapChainImageFormat();
        m_DepthFormat = context->GetDepthFormat();

        // ---------------------------------------------------------------
        // 1. MSAA Color image (attachment 0, multisampled)
        // ---------------------------------------------------------------
        if (useMSAA) {
            CreateImage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                        msaaSamples, m_ColorFormat,
                        m_ColorMSAAImage, m_ColorMSAAMemory);
            m_ColorMSAAView = CreateView(m_ColorMSAAImage, m_ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
            TransitionLayout(m_ColorMSAAImage, m_ColorFormat,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_ASPECT_COLOR_BIT);
        }

        // ---------------------------------------------------------------
        // 2. Resolve Color image (attachment 1 in MSAA, or 0 in non-MSAA)
        //    Will be displayed by ImGui in SHADER_READ_ONLY_OPTIMAL
        // ---------------------------------------------------------------
        CreateImage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_SAMPLE_COUNT_1_BIT, m_ColorFormat,
                    m_ColorResolveImage, m_ColorResolveMemory);
        m_ColorResolveView = CreateView(m_ColorResolveImage, m_ColorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        TransitionLayout(m_ColorResolveImage, m_ColorFormat,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_ASPECT_COLOR_BIT);
        // Also transition to SHADER_READ_ONLY so the barrier in
        // BeginMeshRenderPass (SHADER_READ_ONLY→COLOR_ATTACHMENT) is
        // valid on the very first frame
        TransitionLayout(m_ColorResolveImage, m_ColorFormat,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_ASPECT_COLOR_BIT);

        // ---------------------------------------------------------------
        // 3. Depth image (attachment 2 in MSAA, or 1 in non-MSAA)
        // ---------------------------------------------------------------
        CreateImage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    useMSAA ? msaaSamples : VK_SAMPLE_COUNT_1_BIT,
                    m_DepthFormat,
                    m_DepthMSAAImage, m_DepthMSAAMemory);
        m_DepthMSAAView = CreateView(m_DepthMSAAImage, m_DepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        TransitionLayout(m_DepthMSAAImage, m_DepthFormat,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_ASPECT_DEPTH_BIT);

        // ---------------------------------------------------------------
        // 4. Render Pass — 由 FramebufferSpecification::RenderPass 指定
        //    (Render pass is created and owned by VulkanContext / renderPassesMap)
        // ---------------------------------------------------------------

        // ---------------------------------------------------------------
        // 5. Framebuffer
        // ---------------------------------------------------------------
        {
            VkImageView views[3];
            uint32_t    count;

            if (useMSAA) {
                views[0] = m_ColorMSAAView;
                views[1] = m_ColorResolveView;
                views[2] = m_DepthMSAAView;
                count    = 3;
            } else {
                views[0] = m_ColorResolveView;  // non-MSAA: color is also the display target
                views[1] = m_DepthMSAAView;
                count    = 2;
            }

            VkFramebufferCreateInfo fbInfo = {};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = m_RenderPass;
            fbInfo.attachmentCount = count;
            fbInfo.pAttachments    = views;
            fbInfo.width           = m_Specification.Width;
            fbInfo.height          = m_Specification.Height;
            fbInfo.layers          = 1;

            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_VulkanFramebuffer) != VK_SUCCESS)
                throw std::runtime_error("VulkanFramebuffer: failed to create framebuffer!");
        }

        // ---------------------------------------------------------------
        // 6. Sampler (for ImGui display)
        // ---------------------------------------------------------------
        {
            VkSamplerCreateInfo samplerInfo = {};
            samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter               = VK_FILTER_LINEAR;
            samplerInfo.minFilter               = VK_FILTER_LINEAR;
            samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.anisotropyEnable        = VK_FALSE;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.compareEnable           = VK_FALSE;
            samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.minLod                  = 0.0f;
            samplerInfo.maxLod                  = 1.0f;

            if (vkCreateSampler(device, &samplerInfo, nullptr, &m_ColorSampler) != VK_SUCCESS)
                throw std::runtime_error("VulkanFramebuffer: failed to create sampler!");
        }

        m_ImGuiTextureRegistered = false;
        m_ImGuiDescriptorSet     = VK_NULL_HANDLE;
    }

    // ========================================================================
    // Release
    // ========================================================================
    void VulkanFramebuffer::Release()
    {
        auto* context = VulkanContext::Get();
        if (!context) return;

        auto device = context->GetDevice();
        vkDeviceWaitIdle(device);

        if (m_ImGuiTextureRegistered && m_ImGuiDescriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_ImGuiDescriptorSet);
            m_ImGuiTextureRegistered = false;
            m_ImGuiDescriptorSet     = VK_NULL_HANDLE;
        }

        if (m_ColorSampler        != VK_NULL_HANDLE) { vkDestroySampler(device, m_ColorSampler, nullptr); m_ColorSampler = VK_NULL_HANDLE; }
        if (m_VulkanFramebuffer   != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, m_VulkanFramebuffer, nullptr); m_VulkanFramebuffer = VK_NULL_HANDLE; }

        if (m_ColorMSAAView       != VK_NULL_HANDLE) { vkDestroyImageView(device, m_ColorMSAAView, nullptr); m_ColorMSAAView = VK_NULL_HANDLE; }
        if (m_ColorMSAAImage      != VK_NULL_HANDLE) { vkDestroyImage(device, m_ColorMSAAImage, nullptr); m_ColorMSAAImage = VK_NULL_HANDLE; }
        if (m_ColorMSAAMemory     != VK_NULL_HANDLE) { vkFreeMemory(device, m_ColorMSAAMemory, nullptr); m_ColorMSAAMemory = VK_NULL_HANDLE; }

        if (m_ColorResolveView    != VK_NULL_HANDLE) { vkDestroyImageView(device, m_ColorResolveView, nullptr); m_ColorResolveView = VK_NULL_HANDLE; }
        if (m_ColorResolveImage   != VK_NULL_HANDLE) { vkDestroyImage(device, m_ColorResolveImage, nullptr); m_ColorResolveImage = VK_NULL_HANDLE; }
        if (m_ColorResolveMemory  != VK_NULL_HANDLE) { vkFreeMemory(device, m_ColorResolveMemory, nullptr); m_ColorResolveMemory = VK_NULL_HANDLE; }

        if (m_DepthMSAAView       != VK_NULL_HANDLE) { vkDestroyImageView(device, m_DepthMSAAView, nullptr); m_DepthMSAAView = VK_NULL_HANDLE; }
        if (m_DepthMSAAImage      != VK_NULL_HANDLE) { vkDestroyImage(device, m_DepthMSAAImage, nullptr); m_DepthMSAAImage = VK_NULL_HANDLE; }
        if (m_DepthMSAAMemory     != VK_NULL_HANDLE) { vkFreeMemory(device, m_DepthMSAAMemory, nullptr); m_DepthMSAAMemory = VK_NULL_HANDLE; }
    }

    // ========================================================================
    // ImGui texture registration
    // ========================================================================
    void VulkanFramebuffer::EnsureImGuiTexture()
    {
        if (m_ImGuiTextureRegistered) return;
        if (m_ColorSampler == VK_NULL_HANDLE || m_ColorResolveView == VK_NULL_HANDLE) return;

        // Register resolve image with ImGui — after the render pass it's in
        // SHADER_READ_ONLY_OPTIMAL layout (set in finalLayout of resolve attachment)
        m_ImGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(
            m_ColorSampler,
            m_ColorResolveView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        m_ImGuiTextureRegistered = true;
    }

    uint64_t VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        const_cast<VulkanFramebuffer*>(this)->EnsureImGuiTexture();
        return (uint64_t)m_ImGuiDescriptorSet;
    }

}
