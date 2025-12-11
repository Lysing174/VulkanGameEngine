#include "pch.h"
#include "VulkanTexture.h"
#include "Platform/Vulkan/VulkanContext.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Engine {
    VulkanTexture2D::VulkanTexture2D(const std::string& path)
        : m_Path(path)
    {
		int width, height, channels;
		//stbi_set_flip_vertically_on_load(1);
		std::cout << "Loading Texture: " << path << std::endl;
		stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels) {
			throw std::runtime_error("Failed to load image from path: " + path);
			EG_CORE_ERROR("Failed to load image: {0}", path);
			//return; // 实际项目中应该放一个粉色默认图
		}

		m_Width = width;
		m_Height = height;
		m_Channels = 4;
		m_ImageFormat = VK_FORMAT_R8G8B8A8_SRGB; // 使用 SRGB 进行伽马校正
		VkDeviceSize imageSize = width * height * 4;

		// 创建 Staging Buffer (CPU 可见)
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		VulkanContext::Get()->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
		auto device = VulkanContext::Get()->GetDevice();

		void* data;
		vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		vkUnmapMemory(device, stagingBufferMemory);

		stbi_image_free(pixels);

		// 创建实际的 Image (GPU 专用)
		VulkanContext::Get()->CreateImage(m_Width, m_Height, m_ImageFormat, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Image, m_ImageMemory);

		// 布局转换 + 拷贝 + 布局转换
		VulkanContext::Get()->TransitionImageLayout(m_Image, m_ImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VulkanContext::Get()->CopyBufferToImage(stagingBuffer, m_Image, static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
		VulkanContext::Get()->TransitionImageLayout(m_Image, m_ImageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// 清理 Staging Buffer
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);

		// 创建视图和采样器
		CreateTextureImageView();
		CreateTextureSampler();

		// 填充 Descriptor Info (供后续绑定使用)
		m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		m_DescriptorImageInfo.imageView = m_ImageView;
		m_DescriptorImageInfo.sampler = m_Sampler;
    }

	VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height, void* data)
		: m_Width(width), m_Height(height), m_Channels(4)
	{
		m_ImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
		VkDeviceSize imageSize = width * height * 4;

		// 创建 Staging Buffer (CPU 可见)
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		VulkanContext::Get()->CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
		auto device = VulkanContext::Get()->GetDevice();

		void* dstData;
		vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &dstData);
		memcpy(dstData, data, static_cast<size_t>(imageSize));
		vkUnmapMemory(device, stagingBufferMemory);

		// 创建实际的 Image (GPU 专用)
		VulkanContext::Get()->CreateImage(m_Width, m_Height, m_ImageFormat, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Image, m_ImageMemory);

		// 布局转换 + 拷贝 + 布局转换
		VulkanContext::Get()->TransitionImageLayout(m_Image, m_ImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VulkanContext::Get()->CopyBufferToImage(stagingBuffer, m_Image, static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height));
		VulkanContext::Get()->TransitionImageLayout(m_Image, m_ImageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// 清理 Staging Buffer
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);

		// 创建视图和采样器
		CreateTextureImageView();
		CreateTextureSampler();

		// 填充 Descriptor Info (供后续绑定使用)
		m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		m_DescriptorImageInfo.imageView = m_ImageView;
		m_DescriptorImageInfo.sampler = m_Sampler;

	}

	VulkanTexture2D::~VulkanTexture2D()
	{
		auto device = VulkanContext::Get()->GetDevice();

		vkDestroySampler(device, m_Sampler, nullptr);
		vkDestroyImageView(device, m_ImageView, nullptr);
		vkDestroyImage(device, m_Image, nullptr);
		vkFreeMemory(device, m_ImageMemory, nullptr);
	}
	void VulkanTexture2D::CreateTextureImageView()
	{
		m_ImageView = VulkanContext::Get()->CreateImageView(m_Image, m_ImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	void VulkanTexture2D::CreateTextureSampler()
	{
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(VulkanContext::Get()->GetPhysicalDevice(), &properties);

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR; // 线性过滤
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // 循环平铺
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_TRUE; // 各向异性过滤 (建议开启)
		samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;

		if (vkCreateSampler(VulkanContext::Get()->GetDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
			throw std::runtime_error("failed to create texture sampler!");
		}
	}


    // ... 析构函数负责 vkDestroyImage, vkDestroySampler ...
}