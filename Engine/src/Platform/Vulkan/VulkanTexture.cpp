#include "pch.h"
#include "VulkanTexture.h"
#include "Platform/Vulkan/VulkanContext.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Engine {
    // 内部辅助：从像素数据创建 GPU Image + ImageView + Sampler
    static void CreateTextureFromPixels(
        uint32_t& outWidth, uint32_t& outHeight,
        VkImage& outImage, VkDeviceMemory& outImageMemory,
        VkImageView& outImageView, VkSampler& outSampler,
        VkDescriptorImageInfo& outDescInfo,
        VkFormat format, stbi_uc* pixels)
    {
        VkDeviceSize imageSize = outWidth * outHeight * 4;

        // 创建 Staging Buffer (CPU 可见)
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanContext::Get()->CreateBuffer(imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingBufferMemory);
        auto device = VulkanContext::Get()->GetDevice();

        void* dstData;
        vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &dstData);
        memcpy(dstData, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device, stagingBufferMemory);

        // 创建 GPU Image
        VulkanContext::Get()->CreateImage(outWidth, outHeight, format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outImage, outImageMemory);

        // 布局转换 + 拷贝 + 布局转换
        VulkanContext::Get()->TransitionImageLayout(outImage, format,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VulkanContext::Get()->CopyBufferToImage(stagingBuffer, outImage,
            static_cast<uint32_t>(outWidth), static_cast<uint32_t>(outHeight));
        VulkanContext::Get()->TransitionImageLayout(outImage, format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 清理 Staging Buffer
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        // 创建 ImageView
        outImageView = VulkanContext::Get()->CreateImageView(outImage, format, VK_IMAGE_ASPECT_COLOR_BIT);

        // 创建 Sampler
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(VulkanContext::Get()->GetPhysicalDevice(), &properties);
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
        if (vkCreateSampler(device, &samplerInfo, nullptr, &outSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture sampler!");
        }

        // 填充 Descriptor Info
        outDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outDescInfo.imageView = outImageView;
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

		CreateTextureFromPixels(m_Width, m_Height, m_Image, m_ImageMemory,
			m_ImageView, m_Sampler, m_DescriptorImageInfo, m_ImageFormat, pixels);

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

		CreateTextureFromPixels(m_Width, m_Height, m_Image, m_ImageMemory,
			m_ImageView, m_Sampler, m_DescriptorImageInfo, m_ImageFormat, pixels);

		stbi_image_free(pixels);
	}

	VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height, void* data, VkFormat format)
		: m_Width(width), m_Height(height), m_Channels(4)
	{
		m_ImageFormat = format;

		CreateTextureFromPixels(m_Width, m_Height, m_Image, m_ImageMemory,
			m_ImageView, m_Sampler, m_DescriptorImageInfo, m_ImageFormat, (stbi_uc*)data);
	}

	VulkanTexture2D::~VulkanTexture2D()
	{
		auto device = VulkanContext::Get()->GetDevice();

		vkDestroySampler(device, m_Sampler, nullptr);
		vkDestroyImageView(device, m_ImageView, nullptr);
		vkDestroyImage(device, m_Image, nullptr);
		vkFreeMemory(device, m_ImageMemory, nullptr);

		if (m_MaterialDescriptorSet != VK_NULL_HANDLE)
		{
			vkFreeDescriptorSets(device, VulkanContext::Get()->GetDescriptorPool(), 1, &m_MaterialDescriptorSet);
		}
	}
}