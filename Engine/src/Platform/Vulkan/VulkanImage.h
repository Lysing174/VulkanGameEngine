#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <memory>

namespace Engine {

	// ============================================================
	// VulkanImageSpec — configuration for creating a VulkanImage
	// ============================================================
	struct VulkanImageSpec
	{
		uint32_t Width = 1;
		uint32_t Height = 1;
		uint32_t MipLevels = 1;
		uint32_t ArrayLayers = 1;

		VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
		VkImageTiling Tiling = VK_IMAGE_TILING_OPTIMAL;
		VkImageUsageFlags Usage = 0;
		VkMemoryPropertyFlags MemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		VkImageCreateFlags CreateFlags = 0;
		VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;

		// ImageView settings (for the default full-range view)
		VkImageAspectFlags AspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
		VkImageViewType ViewType = VK_IMAGE_VIEW_TYPE_2D;

		std::string DebugName;
	};

	// ============================================================
	// VulkanImage — barebones VkImage + VkDeviceMemory + VkImageView
	//
	// No sampler, no descriptor set — pure GPU image resource.
	// Compute shaders can bind this directly as a storage image.
	// Texture classes compose this with a sampler for sampling.
	//
	// Supports mip levels and array layers (cubemap = 6 layers).
	// ============================================================
	class VulkanImage
	{
	public:
		explicit VulkanImage(const VulkanImageSpec& spec);
		~VulkanImage();

		// Move semantics
		VulkanImage(VulkanImage&& other) noexcept;
		VulkanImage& operator=(VulkanImage&& other) noexcept;

		// No copy (GPU resources are not shareable by value)
		VulkanImage(const VulkanImage&) = delete;
		VulkanImage& operator=(const VulkanImage&) = delete;

		// ======== Accessors ========

		VkImage GetImage() const { return m_Image; }
		VkImageView GetImageView() const { return m_View; }
		VkFormat GetFormat() const { return m_Spec.Format; }
		uint32_t GetWidth() const { return m_Spec.Width; }
		uint32_t GetHeight() const { return m_Spec.Height; }
		uint32_t GetMipLevels() const { return m_Spec.MipLevels; }
		uint32_t GetArrayLayers() const { return m_Spec.ArrayLayers; }
		const VulkanImageSpec& GetSpec() const { return m_Spec; }

		// ======== Convenience helpers ========

		// Transition the entire image (all mips, all layers)
		void TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout) const;

		// Transition a subresource range (specific mips/layers)
		void TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout,
			uint32_t baseMip, uint32_t mipCount,
			uint32_t baseLayer, uint32_t layerCount) const;

		// Create a per-mip-layer ImageView (caller must destroy it)
		// Useful for compute shader per-mip output binding
		VkImageView CreateView(uint32_t baseMip, uint32_t mipCount,
			uint32_t baseLayer, uint32_t layerCount) const;

	private:
		void Destroy();

		VulkanImageSpec m_Spec;

		VkImage m_Image = VK_NULL_HANDLE;
		VkDeviceMemory m_Memory = VK_NULL_HANDLE;
		VkImageView m_View = VK_NULL_HANDLE; // Full-range view (all mips, all layers)
	};

} // namespace Engine
