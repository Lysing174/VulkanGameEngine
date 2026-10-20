#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <Engine/Renderer/Shader.h>  // ShaderConfig

namespace Engine {

	class VulkanComputeShader
	{
	public:
		// Constructor: reads .spv file, creates VkShaderModule
		explicit VulkanComputeShader(const std::string& spvPath);

		// 构造函数重载 (区别于 VS/FS): 仅 .spv 路径 + ShaderConfig (描述 set 绑定 / push constant,
		// 供后续 compute pipeline layout 创建; 此时仅存配置, 模块创建同单参构造).
		VulkanComputeShader(const std::string& spvPath, const ShaderConfig& config);
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
		const std::string& GetName() const { return m_Name; }
		const ShaderConfig& GetConfig() const { return m_Config; }

	private:
		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code);
		void DestroyPipeline();
		void DestroyResources();

		VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;
		std::string m_SpvPath;
		std::string m_Name;          // .spv 路径 basename (库内命名)
		ShaderConfig m_Config;       // 计算着色器配置 (set 绑定 / push constant)
	};

} // namespace Engine
