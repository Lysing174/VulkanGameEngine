#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

namespace Engine {

class VulkanCubeTexture;
class VulkanComputeShader;

// ============================================================
// MipmapGenerator
//
// GPU-based GGX pre-filtered mipmap generator for cubemaps.
// Replaces CPU box-filter dowmsample with importance-sampled
// GGX prefiltering for high-quality IBL specular reflections.
//
// Each mip level corresponds to a roughness value:
//   mip 0 → roughness 0 (base env map, unmodified)
//   mip k → roughness = k / (N-1),  k ∈ [1, N-1]
//
// The compute shader uses Hammersley low-discrepancy sequence
// to importance-sample the GGX NDF, producing a pre-convolved
// environment map suitable for split-sum IBL.
// ============================================================
class MipmapGenerator
{
public:
	MipmapGenerator();
	~MipmapGenerator();

	// No copy
	MipmapGenerator(const MipmapGenerator&) = delete;
	MipmapGenerator& operator=(const MipmapGenerator&) = delete;

	// Move semantics
	MipmapGenerator(MipmapGenerator&&) noexcept = default;
	MipmapGenerator& operator=(MipmapGenerator&&) noexcept = default;

	// Generate GGX pre-filtered mipmaps for a cubemap.
	// The cubemap must have its base level (mip 0) already filled.
	// The cubemap must have been created with VK_IMAGE_USAGE_STORAGE_BIT
	// and must be in VK_IMAGE_LAYOUT_GENERAL layout.
	// All mip levels 1..N-1 will be populated via compute shader.
	void Generate(VulkanCubeTexture* cubemap);

	// Pre-generated BRDF integration LUT (optional, not yet used)
	void GenerateBRDFLUT();

private:
	void CreateDescriptorSetLayout();
	void CreatePipelineLayout();
	void CreatePipeline();
	void CreateDescriptorPool(uint32_t maxMipLevels);
	void CreatePerMipResources(VulkanCubeTexture* cubemap);

	// Create 2D_ARRAY image views per mip (for storage image writes)
	void CreateMipWriteViews(VkImage image, VkFormat format,
		uint32_t mipLevels, uint32_t faceSize);

	// Dispatch compute for a single mip level
	void DispatchMip(VkCommandBuffer cmd, uint32_t mip, uint32_t faceSize,
		VkDescriptorSet descSet);

	// Cleanup previously allocated per-mip resources
	void DestroyPerMipResources();

private:
	static constexpr uint32_t kLocalSizeX = 8;
	static constexpr uint32_t kLocalSizeY = 8;
	static constexpr uint32_t kSampleCount = 256;

	std::unique_ptr<VulkanComputeShader> m_ComputeShader;

	VkDescriptorSetLayout  m_DescriptorSetLayout  = VK_NULL_HANDLE;
	VkPipelineLayout        m_PipelineLayout       = VK_NULL_HANDLE;
	VkDescriptorPool        m_DescriptorPool       = VK_NULL_HANDLE;

	// 2D_ARRAY views — one per mip level — for compute shader storage write
	std::vector<VkImageView> m_MipWriteViews;

	// One descriptor set per mip level (source cubemap is shared)
	std::vector<VkDescriptorSet> m_DescriptorSets;

	// Cached cubemap info
	uint32_t m_TotalMips = 0;
};

} // namespace Engine
