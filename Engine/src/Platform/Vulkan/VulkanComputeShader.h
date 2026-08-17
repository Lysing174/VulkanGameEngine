#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace Engine {

	class VulkanComputeShader
	{
	public:
		// Constructor: reads .spv file, creates VkShaderModule
		explicit VulkanComputeShader(const std::string& spvPath);
		~VulkanComputeShader();

		// Move semantics (RAII-safe — transfers ownership, zeroes source)
		VulkanComputeShader(VulkanComputeShader&& other) noexcept;
		VulkanComputeShader& operator=(VulkanComputeShader&& other) noexcept;

		// No copy
		VulkanComputeShader(const VulkanComputeShader&) = delete;
		VulkanComputeShader& operator=(const VulkanComputeShader&) = delete;

		// Create compute pipeline from a pipeline layout, cache it internally.
		// Safe to call multiple times; old pipeline is destroyed before creating a new one.
		void CreatePipeline(VkPipelineLayout pipelineLayout);

		// Bind pipeline + layout + descriptor set, then dispatch with (x, y, z) workgroups.
		// Pipeline must have been created via CreatePipeline() first.
		void Dispatch(VkCommandBuffer cmd, VkPipelineLayout layout,
					  VkDescriptorSet descSet, uint32_t x, uint32_t y, uint32_t z) const;

		// Accessors
		VkPipeline GetPipeline() const { return m_Pipeline; }
		VkShaderModule GetShaderModule() const { return m_ShaderModule; }

	private:
		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code);
		void DestroyPipeline();
		void DestroyResources();

		VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
		std::string m_SpvPath;
	};

} // namespace Engine
