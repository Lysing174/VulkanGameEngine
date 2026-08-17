#pragma once

#include "Platform/Vulkan/VulkanImage.h"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>

namespace Engine {

	// ============================================================
	// VulkanCubeTexture — cube map image with 6 array layers
	//
	// Composed of a VulkanImage (6-layer, cubemap-compatible) +
	// a cubemap sampler. Used for environment maps, skyboxes, etc.
	// The underlying VulkanImage is accessible for compute shader
	// storage image binding (per-mip views via CreateView).
	// ============================================================
	class VulkanCubeTexture
	{
	public:
		// Create an empty cubemap image (for compute shader output, e.g. GGX prefilter)
		// width/height: face resolution
		// mipLevels:    number of mip levels (1 = base only)
		// format:       typically R16G16B16A16_SFLOAT for HDR environment maps
		VulkanCubeTexture(uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
			VkImageUsageFlags extraUsage = 0);

		~VulkanCubeTexture();

		// Move semantics
		VulkanCubeTexture(VulkanCubeTexture&&) noexcept;
		VulkanCubeTexture& operator=(VulkanCubeTexture&&) noexcept;

		// No copy
		VulkanCubeTexture(const VulkanCubeTexture&) = delete;
		VulkanCubeTexture& operator=(const VulkanCubeTexture&) = delete;

		// ======== Accessors ========

		const VulkanImage& GetImage() const { return *m_Image; }
		VkSampler GetSampler() const { return m_Sampler; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetMipLevels() const { return m_MipLevels; }
		VkFormat GetFormat() const { return m_Format; }

		// Per-mip ImageView (all 6 layers) — for compute shader per-mip output
		// Caller is responsible for destroying returned VkImageView via vkDestroyImageView
		VkImageView CreateMipView(uint32_t mip) const;

		// Descriptor image info for shader binding
		const VkDescriptorImageInfo& GetDescriptorImageInfo() const { return m_DescriptorInfo; }

		// Update the stored image layout (e.g. after a layout transition).
		// The external VkDescriptorImageInfo will reflect the new layout.
		void SetDescriptorLayout(VkImageLayout layout) { m_DescriptorInfo.imageLayout = layout; }

	private:
		void CreateSampler();

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_MipLevels = 1;
		VkFormat m_Format = VK_FORMAT_R16G16B16A16_SFLOAT;

		std::unique_ptr<VulkanImage> m_Image;
		VkSampler m_Sampler = VK_NULL_HANDLE;
		VkDescriptorImageInfo m_DescriptorInfo{};
	};

} // namespace Engine
