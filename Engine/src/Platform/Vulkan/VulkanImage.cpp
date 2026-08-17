#include "pch.h"
#include "VulkanImage.h"
#include "Platform/Vulkan/VulkanContext.h"

namespace Engine {

	// -----------------------------------------------------------------------
	// Static helpers — raw Vk resource creation, no VulkanContext dependency
	// -----------------------------------------------------------------------
	static void CreateVkImage(VkDevice device, uint32_t width, uint32_t height,
		uint32_t mipLevels, uint32_t arrayLayers, VkImageCreateFlags flags,
		VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling,
		VkImageUsageFlags usage, VkMemoryPropertyFlags memFlags,
		VkImage& outImage, VkDeviceMemory& outMemory)
	{
		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent = { width, height, 1 };
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = arrayLayers;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = usage;
		imageInfo.samples = samples;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.flags = flags;

		if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
			throw std::runtime_error("VulkanImage: failed to create image!");

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(device, outImage, &memReq);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = VulkanContext::Get()->FindMemoryType(
			memReq.memoryTypeBits, memFlags);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
			throw std::runtime_error("VulkanImage: failed to allocate image memory!");

		vkBindImageMemory(device, outImage, outMemory, 0);
	}

	static VkImageView CreateVkImageView(VkDevice device, VkImage image,
		VkFormat format, VkImageAspectFlags aspectFlags,
		VkImageViewType viewType, uint32_t baseMip, uint32_t mipCount,
		uint32_t baseLayer, uint32_t layerCount)
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = viewType;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = baseMip;
		viewInfo.subresourceRange.levelCount = mipCount;
		viewInfo.subresourceRange.baseArrayLayer = baseLayer;
		viewInfo.subresourceRange.layerCount = layerCount;

		VkImageView imageView;
		if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
			throw std::runtime_error("VulkanImage: failed to create image view!");

		return imageView;
	}

	// ===================================================================
	// VulkanImage implementation
	// ===================================================================

	VulkanImage::VulkanImage(const VulkanImageSpec& spec)
		: m_Spec(spec)
	{
		auto device = VulkanContext::Get()->GetDevice();

		CreateVkImage(device,
			spec.Width, spec.Height,
			spec.MipLevels, spec.ArrayLayers,
			spec.CreateFlags, spec.Samples,
			spec.Format, spec.Tiling,
			spec.Usage, spec.MemoryFlags,
			m_Image, m_Memory
		);

		m_View = CreateVkImageView(device, m_Image,
			spec.Format, spec.AspectFlags, spec.ViewType,
			0, spec.MipLevels,
			0, spec.ArrayLayers
		);

		if (!spec.DebugName.empty())
			EG_CORE_INFO("Created VulkanImage: {0} ({1}x{2}, {3} mips, {4} layers)",
				spec.DebugName, spec.Width, spec.Height, spec.MipLevels, spec.ArrayLayers);
	}

	VulkanImage::~VulkanImage()
	{
		Destroy();
	}

	VulkanImage::VulkanImage(VulkanImage&& other) noexcept
		: m_Spec(std::move(other.m_Spec))
		, m_Image(other.m_Image)
		, m_Memory(other.m_Memory)
		, m_View(other.m_View)
	{
		other.m_Image = VK_NULL_HANDLE;
		other.m_Memory = VK_NULL_HANDLE;
		other.m_View = VK_NULL_HANDLE;
	}

	VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept
	{
		if (this != &other)
		{
			Destroy();
			m_Spec = std::move(other.m_Spec);
			m_Image = other.m_Image;
			m_Memory = other.m_Memory;
			m_View = other.m_View;
			other.m_Image = VK_NULL_HANDLE;
			other.m_Memory = VK_NULL_HANDLE;
			other.m_View = VK_NULL_HANDLE;
		}
		return *this;
	}

	// -------------------------------------------------------------------
	// TransitionImageLayout — internal helper
	// -------------------------------------------------------------------
	static bool HasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	static void TransitionLayoutImpl(VkImage image, VkFormat format,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		uint32_t baseMip, uint32_t mipCount,
		uint32_t baseLayer, uint32_t layerCount)
	{
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.baseMipLevel = baseMip;
		barrier.subresourceRange.levelCount = mipCount;
		barrier.subresourceRange.baseArrayLayer = baseLayer;
		barrier.subresourceRange.layerCount = layerCount;

		VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (HasStencilComponent(format))
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		else
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else {
			EG_CORE_ERROR("Unsupported layout transition: {0} -> {1}", (int)oldLayout, (int)newLayout);
			throw std::invalid_argument("unsupported layout transition!");
		}

		auto ctx = VulkanContext::Get();
		VkCommandBuffer cmd = ctx->BeginSingleTimeCommands();
		vkCmdPipelineBarrier(cmd, sourceStage, destinationStage,
			0, 0, nullptr, 0, nullptr, 1, &barrier);
		ctx->EndSingleTimeCommands(cmd);
	}

	// ===================================================================
	// Public API
	// ===================================================================

	void VulkanImage::TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout) const
	{
		TransitionLayout(oldLayout, newLayout, 0, m_Spec.MipLevels, 0, m_Spec.ArrayLayers);
	}

	void VulkanImage::TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout,
		uint32_t baseMip, uint32_t mipCount,
		uint32_t baseLayer, uint32_t layerCount) const
	{
		TransitionLayoutImpl(m_Image, m_Spec.Format,
			oldLayout, newLayout,
			baseMip, mipCount,
			baseLayer, layerCount);
	}

	VkImageView VulkanImage::CreateView(uint32_t baseMip, uint32_t mipCount,
		uint32_t baseLayer, uint32_t layerCount) const
	{
		return CreateVkImageView(VulkanContext::Get()->GetDevice(),
			m_Image, m_Spec.Format, m_Spec.AspectFlags, m_Spec.ViewType,
			baseMip, mipCount,
			baseLayer, layerCount);
	}

	void VulkanImage::Destroy()
	{
		auto device = VulkanContext::Get()->GetDevice();

		if (m_View != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, m_View, nullptr);
			m_View = VK_NULL_HANDLE;
		}
		if (m_Image != VK_NULL_HANDLE)
		{
			vkDestroyImage(device, m_Image, nullptr);
			m_Image = VK_NULL_HANDLE;
		}
		if (m_Memory != VK_NULL_HANDLE)
		{
			vkFreeMemory(device, m_Memory, nullptr);
			m_Memory = VK_NULL_HANDLE;
		}
	}

} // namespace Engine
