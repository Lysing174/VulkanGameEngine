#include "pch.h"
#include "VulkanTexture.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Engine {

    // 内部辅助：创建默认的 2D sampler
    static VkSampler CreateDefaultSampler()
    {
        auto* ctx = VulkanContext::Get();
        auto device = ctx->GetDevice();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(ctx->GetPhysicalDevice(), &properties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        VkSampler sampler;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture sampler!");
        }
        return sampler;
    }

    // 内部辅助：从像素数据创建 VulkanImage + Sampler
    static void CreateTextureFromPixels(
        uint32_t& outWidth, uint32_t& outHeight,
        std::unique_ptr<VulkanImage>& outImage,
        VkSampler& outSampler,
        VkDescriptorImageInfo& outDescInfo,
        VkFormat format, stbi_uc* pixels)
    {
        auto* ctx = VulkanContext::Get();
        VkDeviceSize imageSize = outWidth * outHeight * 4;

        // 创建 Staging Buffer (CPU 可见)
        VulkanBuffer staging(imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.SetData(pixels, imageSize);

        // 创建 VulkanImage (1 mip, 1 layer, transfer dst + sampled usage)
        VulkanImageSpec spec;
        spec.Width = outWidth;
        spec.Height = outHeight;
        spec.MipLevels = 1;
        spec.ArrayLayers = 1;
        spec.Format = format;
        spec.Tiling = VK_IMAGE_TILING_OPTIMAL;
        spec.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        spec.MemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        spec.AspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
        spec.ViewType = VK_IMAGE_VIEW_TYPE_2D;
        spec.DebugName = "Texture2D";

        outImage = std::make_unique<VulkanImage>(spec);

        // 布局转换 → 拷贝 → 布局转换
        outImage->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        ctx->CopyBufferToImage(staging.GetBuffer(), outImage->GetImage(),
            static_cast<uint32_t>(outWidth), static_cast<uint32_t>(outHeight));
        outImage->TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // staging buffer 在作用域结束时自动销毁

        // 创建 Sampler
        outSampler = CreateDefaultSampler();

        // 填充 Descriptor Info
        outDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outDescInfo.imageView = outImage->GetImageView();
        outDescInfo.sampler = outSampler;
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path, VkFormat format)
        : m_Path(path)
    {
        int width, height, channels;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            EG_CORE_ERROR("Failed to load image: {0}", path);
            throw std::runtime_error("Failed to load image from path: " + path);
        }

        m_Width = width;
        m_Height = height;
        m_Channels = 4;
        m_ImageFormat = format;

        CreateTextureFromPixels(m_Width, m_Height, m_Image,
            m_Sampler, m_DescriptorImageInfo, m_ImageFormat, pixels);

        stbi_image_free(pixels);
    }

    VulkanTexture2D::VulkanTexture2D(const void* data, size_t size, VkFormat format)
    {
        int width, height, channels;
        stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)data, (int)size,
            &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            EG_CORE_ERROR("Failed to load image from memory: {0}", stbi_failure_reason());
            throw std::runtime_error("Failed to load image from memory");
        }

        m_Width = width;
        m_Height = height;
        m_Channels = 4;
        m_ImageFormat = format;

        CreateTextureFromPixels(m_Width, m_Height, m_Image,
            m_Sampler, m_DescriptorImageInfo, m_ImageFormat, pixels);

        stbi_image_free(pixels);
    }

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height, void* data, VkFormat format)
        : m_Width(width), m_Height(height), m_Channels(4)
    {
        m_ImageFormat = format;

        CreateTextureFromPixels(m_Width, m_Height, m_Image,
            m_Sampler, m_DescriptorImageInfo, m_ImageFormat, (stbi_uc*)data);
    }

    VulkanTexture2D::~VulkanTexture2D()
    {
        auto device = VulkanContext::Get()->GetDevice();

        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_Sampler, nullptr);
        }
        if (m_MaterialDescriptorSet != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(device, VulkanContext::Get()->GetDescriptorPool(), 1, &m_MaterialDescriptorSet);
        }
        // VulkanImage is destroyed via unique_ptr destructor
    }

    uint32_t VulkanTexture2D::GetRendererID() const
    {
        // ImGui uses this to display the texture — return the descriptor set if available,
        // otherwise fall back to the image pointer (for ImGui binding in other paths)
        if (m_MaterialDescriptorSet != VK_NULL_HANDLE)
            return (uint32_t)(uint64_t)m_MaterialDescriptorSet;
        return (uint32_t)(uint64_t)m_Image->GetImageView();
    }

} // namespace Engine
