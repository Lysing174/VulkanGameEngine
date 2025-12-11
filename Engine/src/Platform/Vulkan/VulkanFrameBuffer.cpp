#include "pch.h"
#include "VulkanFrameBuffer.h"

namespace Engine {
    // Platform/Vulkan/VulkanFramebuffer.cpp

    uint64_t VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        // 这里将 VkDescriptorSet 句柄转换成 uint64_t
        // 注意：你必须保证 m_DescriptorSet 是有效的
        return (uint64_t)m_DescriptorSet;
    }
}