#include "pch.h"
#include "VulkanCubeTexture.h"
#include "Platform/Vulkan/VulkanContext.h"

namespace Engine {

	VulkanCubeTexture::VulkanCubeTexture(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
		VkImageUsageFlags extraUsage)
		: m_Width(width), m_Height(height), m_MipLevels(mipLevels), m_Format(format)
	{
		// Cubemap needs 6 array layers + VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
		VulkanImageSpec spec;
		spec.Width = width;
		spec.Height = height;
		spec.MipLevels = mipLevels;
		spec.ArrayLayers = 6;
		spec.Format = format;
		spec.Tiling = VK_IMAGE_TILING_OPTIMAL;
		spec.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT | extraUsage;
		spec.MemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		spec.CreateFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		spec.AspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		spec.ViewType = VK_IMAGE_VIEW_TYPE_CUBE;
		spec.DebugName = "CubeTexture";

		m_Image = std::make_unique<VulkanImage>(spec);

		// Initialize to UNDEFINED → GENERAL (so compute shaders can write immediately)
		m_Image->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		CreateSampler();

		// Pre-fill descriptor info
		m_DescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		m_DescriptorInfo.imageView = m_Image->GetImageView();
		m_DescriptorInfo.sampler = m_Sampler;

		EG_CORE_INFO("Created CubeTexture: {0}x{1}, {2} mips", width, height, mipLevels);
	}

	VulkanCubeTexture::~VulkanCubeTexture()
	{
		auto device = VulkanContext::Get()->GetDevice();
		if (m_Sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, m_Sampler, nullptr);
		}
		// VulkanImage destroyed via unique_ptr
	}

	VulkanCubeTexture::VulkanCubeTexture(VulkanCubeTexture&& other) noexcept
		: m_Width(other.m_Width)
		, m_Height(other.m_Height)
		, m_MipLevels(other.m_MipLevels)
		, m_Format(other.m_Format)
		, m_Image(std::move(other.m_Image))
		, m_Sampler(other.m_Sampler)
		, m_DescriptorInfo(other.m_DescriptorInfo)
	{
		other.m_Sampler = VK_NULL_HANDLE;
	}

	VulkanCubeTexture& VulkanCubeTexture::operator=(VulkanCubeTexture&& other) noexcept
	{
		if (this != &other)
		{
			auto device = VulkanContext::Get()->GetDevice();
			if (m_Sampler != VK_NULL_HANDLE)
				vkDestroySampler(device, m_Sampler, nullptr);

			m_Width = other.m_Width;
			m_Height = other.m_Height;
			m_MipLevels = other.m_MipLevels;
			m_Format = other.m_Format;
			m_Image = std::move(other.m_Image);
			m_Sampler = other.m_Sampler;
			m_DescriptorInfo = other.m_DescriptorInfo;

			other.m_Sampler = VK_NULL_HANDLE;
		}
		return *this;
	}

	void VulkanCubeTexture::CreateSampler()
	{
		auto device = VulkanContext::Get()->GetDevice();

		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(VulkanContext::Get()->GetPhysicalDevice(), &properties);

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = static_cast<float>(m_MipLevels);

		if (vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
			throw std::runtime_error("failed to create cubemap sampler!");
		}
	}

	VkImageView VulkanCubeTexture::CreateMipView(uint32_t mip) const
	{
		return m_Image->CreateView(mip, 1, 0, 6);
	}

} // namespace Engine
