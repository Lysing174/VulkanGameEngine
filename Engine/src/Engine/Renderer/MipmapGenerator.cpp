#include "pch.h"
#include "MipmapGenerator.h"

#include "Platform/Vulkan/VulkanCubeTexture.h"
#include "Platform/Vulkan/VulkanImage.h"
#include "Platform/Vulkan/VulkanComputeShader.h"
#include "Platform/Vulkan/VulkanContext.h"

#include <cmath>
#include <algorithm>

namespace Engine {

// ===================================================================
// Constructor / Destructor
// ===================================================================
MipmapGenerator::MipmapGenerator()
{
}

MipmapGenerator::~MipmapGenerator()
{
	DestroyPerMipResources();

	auto device = VulkanContext::Get()->GetDevice();

	if (m_DescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
		m_DescriptorPool = VK_NULL_HANDLE;
	}

	if (m_PipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
		m_PipelineLayout = VK_NULL_HANDLE;
	}

	if (m_DescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
		m_DescriptorSetLayout = VK_NULL_HANDLE;
	}
}

// ===================================================================
// DestroyPerMipResources
// ===================================================================
void MipmapGenerator::DestroyPerMipResources()
{
	auto device = VulkanContext::Get()->GetDevice();

	for (auto& view : m_MipWriteViews)
	{
		if (view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, view, nullptr);
		}
	}
	m_MipWriteViews.clear();
	m_DescriptorSets.clear();
	m_TotalMips = 0;
}

// ===================================================================
// CreateDescriptorSetLayout
//
// Binding 0: combined image sampler (samplerCube — read source mip 0)
// Binding 1: storage image (image2DArray — write current mip)
// ===================================================================
void MipmapGenerator::CreateDescriptorSetLayout()
{
	auto device = VulkanContext::Get()->GetDevice();

	// Binding 0: source cubemap (combined image sampler)
	VkDescriptorSetLayoutBinding samplerBinding{};
	samplerBinding.binding = 0;
	samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerBinding.descriptorCount = 1;
	samplerBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	samplerBinding.pImmutableSamplers = nullptr;

	// Binding 1: output mip (storage image, 2D array)
	VkDescriptorSetLayoutBinding storageBinding{};
	storageBinding.binding = 1;
	storageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	storageBinding.descriptorCount = 1;
	storageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	storageBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding bindings[] = { samplerBinding, storageBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 2;
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
	{
		throw std::runtime_error("MipmapGenerator: failed to create descriptor set layout!");
	}
}

// ===================================================================
// CreatePipelineLayout (with push constants for roughness, mip, size)
// ===================================================================
void MipmapGenerator::CreatePipelineLayout()
{
	auto device = VulkanContext::Get()->GetDevice();

	// Push constants: roughness (float), mipLevel (uint), faceSize (uint)
	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushRange.offset = 0;
	pushRange.size = 3 * sizeof(float); // float roughness, uint mipLevel, uint faceSize (all packed as float)

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushRange;

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
	{
		throw std::runtime_error("MipmapGenerator: failed to create pipeline layout!");
	}
}

// ===================================================================
// CreatePipeline — loads GGXPrefilter.comp.spv
// ===================================================================
void MipmapGenerator::CreatePipeline()
{
	m_ComputeShader = std::make_unique<VulkanComputeShader>("shaders/GGXPrefilter.comp.spv");
	m_ComputeShader->CreatePipeline(m_PipelineLayout);

	EG_CORE_INFO("MipmapGenerator: compute pipeline created");
}

// ===================================================================
// CreateDescriptorPool
// ===================================================================
void MipmapGenerator::CreateDescriptorPool(uint32_t maxMipLevels)
{
	auto device = VulkanContext::Get()->GetDevice();

	VkDescriptorPoolSize poolSizes[2]{};
	// Combined image sampler: one per descriptor set (source cubemap)
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount = maxMipLevels;
	// Storage image: one per descriptor set (output mip)
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSizes[1].descriptorCount = maxMipLevels;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 2;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = maxMipLevels;

	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
	{
		throw std::runtime_error("MipmapGenerator: failed to create descriptor pool!");
	}
}

// ===================================================================
// CreateMipWriteViews — 2D_ARRAY views for storage image writes
//
// In Vulkan, cubemap views cannot be used as storage images.
// We create per-mip 2D_ARRAY views (6 layers) for compute writes.
// ===================================================================
void MipmapGenerator::CreateMipWriteViews(VkImage image, VkFormat format,
	uint32_t mipLevels, uint32_t faceSize)
{
	auto device = VulkanContext::Get()->GetDevice();

	// We start from mip 1 (mip 0 is the source, not written)
	for (uint32_t mip = 1; mip < mipLevels; ++mip)
	{
		uint32_t mipSize = std::max(1u, faceSize >> mip);

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = mip;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;

		// Swizzle: keep identity mapping for RGBA
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		VkImageView view;
		if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS)
		{
			throw std::runtime_error("MipmapGenerator: failed to create 2D_ARRAY mip write view!");
		}
		m_MipWriteViews.push_back(view);
	}
}

// ===================================================================
// CreatePerMipResources
//
// For each mip level 1..N-1:
//   - Create 2D_ARRAY view for storage image writes
//   - Allocate descriptor set: samplerCube(source) + image2DArray(mip)
// ===================================================================
void MipmapGenerator::CreatePerMipResources(VulkanCubeTexture* cubemap)
{
	DestroyPerMipResources();

	auto device = VulkanContext::Get()->GetDevice();
	const VulkanImage& image = cubemap->GetImage();
	uint32_t totalMips = cubemap->GetMipLevels();
	uint32_t faceSize = cubemap->GetWidth();

	m_TotalMips = totalMips;

	if (totalMips <= 1)
	{
		EG_CORE_WARN("MipmapGenerator: cubemap has only 1 mip level, nothing to generate");
		return;
	}

	// Create 2D_ARRAY views for mip 1..N-1
	CreateMipWriteViews(image.GetImage(), image.GetFormat(), totalMips, faceSize);

	// Create descriptor sets (one per output mip)
	uint32_t numOutputMips = totalMips - 1; // mips 1..N-1

	// Allocate descriptor sets
	std::vector<VkDescriptorSetLayout> layouts(numOutputMips, m_DescriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_DescriptorPool;
	allocInfo.descriptorSetCount = numOutputMips;
	allocInfo.pSetLayouts = layouts.data();

	m_DescriptorSets.resize(numOutputMips);
	if (vkAllocateDescriptorSets(device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS)
	{
		throw std::runtime_error("MipmapGenerator: failed to allocate descriptor sets!");
	}

	// Source cubemap descriptor info (binding 0 — shared across all sets)
	VkDescriptorImageInfo srcInfo{};
	srcInfo.sampler = cubemap->GetSampler();
	srcInfo.imageView = image.GetImageView(); // Full cubemap view (all mips, all layers)
	srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Source must be in GENERAL layout for compute

	// Write each descriptor set
	for (uint32_t i = 0; i < numOutputMips; ++i)
	{
		uint32_t mip = i + 1; // output mip index

		VkDescriptorImageInfo dstInfo{};
		dstInfo.sampler = VK_NULL_HANDLE;
		dstInfo.imageView = m_MipWriteViews[i];
		dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet writes[2]{};

		// Binding 0: source cubemap sampler
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_DescriptorSets[i];
		writes[0].dstBinding = 0;
		writes[0].dstArrayElement = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].descriptorCount = 1;
		writes[0].pImageInfo = &srcInfo;

		// Binding 1: output mip storage image
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_DescriptorSets[i];
		writes[1].dstBinding = 1;
		writes[1].dstArrayElement = 0;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &dstInfo;

		vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
	}

	EG_CORE_INFO("MipmapGenerator: created per-mip resources ({0} output mips)", numOutputMips);
}

// ===================================================================
// DispatchMip — record compute dispatch for one mip level
// ===================================================================
void MipmapGenerator::DispatchMip(VkCommandBuffer cmd, uint32_t mip, uint32_t faceSize,
	VkDescriptorSet descSet)
{
	uint32_t mipSize = std::max(1u, faceSize >> mip);
	float roughness = static_cast<float>(mip) / static_cast<float>(m_TotalMips - 1);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputeShader->GetPipeline());
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_PipelineLayout,
		0, 1, &descSet, 0, nullptr);

	// Push constants: float roughness, uint mipLevel, uint faceSize
	struct PushBlock { float roughness; float mipLevel; float faceSize; };
	PushBlock push = { roughness, static_cast<float>(mip), static_cast<float>(faceSize) };
	vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
		0, sizeof(PushBlock), &push);

	uint32_t workGroupsX = (mipSize + kLocalSizeX - 1) / kLocalSizeX;
	uint32_t workGroupsY = (mipSize + kLocalSizeY - 1) / kLocalSizeY;
	vkCmdDispatch(cmd, workGroupsX, workGroupsY, 6); // 6 layers (faces)
}

// ===================================================================
// Generate — main entry point
// ===================================================================
void MipmapGenerator::Generate(VulkanCubeTexture* cubemap)
{
	if (!cubemap)
	{
		EG_CORE_ERROR("MipmapGenerator::Generate: null cubemap");
		return;
	}

	uint32_t totalMips = cubemap->GetMipLevels();
	if (totalMips <= 1)
	{
		EG_CORE_WARN("MipmapGenerator::Generate: cubemap has only 1 mip, skipping");
		return;
	}

	// Lazy-init pipeline resources
	if (m_ComputeShader == nullptr)
	{
		CreateDescriptorSetLayout();
		CreatePipelineLayout();
		CreateDescriptorPool(totalMips);
		CreatePipeline();
	}

	// Create per-mip 2D_ARRAY views and descriptor sets
	CreatePerMipResources(cubemap);

	uint32_t faceSize = cubemap->GetWidth();
	uint32_t numOutputMips = totalMips - 1;

	// Cubemap is already in GENERAL layout (ensured by caller).
	// Read from mip 0 via sampler, write to mips 1..N-1 via storage images.
	const VulkanImage& image = cubemap->GetImage();

	// Record compute dispatches
	auto* ctx = VulkanContext::Get();
	VkCommandBuffer cmd = ctx->BeginSingleTimeCommands();

	for (uint32_t i = 0; i < numOutputMips; ++i)
	{
		uint32_t mip = i + 1;
		DispatchMip(cmd, mip, faceSize, m_DescriptorSets[i]);
	}

	// Image memory barrier: ensure all compute writes are visible
	// before the cubemap is used for sampling in graphics pipeline
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 1;
	barrier.subresourceRange.levelCount = numOutputMips;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 6;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	ctx->EndSingleTimeCommands(cmd);

	EG_CORE_INFO("MipmapGenerator: GGX prefilter complete ({0} mips, {1}x{1} faces, {2} samples)",
		totalMips, faceSize, kSampleCount);
}

// ===================================================================
// GenerateBRDFLUT — placeholder for future BRDF integration LUT
// ===================================================================
void MipmapGenerator::GenerateBRDFLUT()
{
	// TODO: generate a 2D BRDF integration LUT texture
	// (n·ω₀) × roughness → 2-component scale+bias
	// Used for the second part of the split-sum approximation
}

} // namespace Engine
