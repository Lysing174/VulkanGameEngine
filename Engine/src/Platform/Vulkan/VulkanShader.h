#pragma once

#include <Engine/Renderer/Shader.h>
#include <Platform/Vulkan/VulkanBuffer.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	class VulkanShader : public Shader
	{
	public:
		VulkanShader(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config);
		virtual ~VulkanShader();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		virtual void CreatePipeline(const std::shared_ptr<BufferLayout>& bufferLayout, std::string pipelineName) override;

		VkShaderModule GetVertexShaderModule() const { return m_VertexShaderModule; }
		VkShaderModule GetFragmentShaderModule() const { return m_FragmentShaderModule; }

		virtual const std::string& GetName() const override { return m_Name; }
		virtual uint32_t GetRendererID() const override { return m_RendererID; }
		virtual const VkPipelineLayout GetPipelineLayout() const override { return m_PipelineLayout; }

		// 获取配置
		virtual const ShaderConfig& GetConfig() const override { return m_Config; }
		virtual void SetSamples(VkSampleCountFlagBits samples) override { m_Config.PipelineState.Samples = samples; }

		// 获取 DescriptorSetLayout (创建 Material 时需要)
		VkDescriptorSetLayout GetMaterialDescriptorSetLayout() const { return m_MaterialDescriptorSetLayout; }

		// 按 RenderPass 名称获取 Pipeline
		VkPipeline GetPipeline(const std::string& name) const;

	private:
		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code);
		void CreateDescriptorSetLayout();

	private:
		VkShaderModule m_VertexShaderModule;
		VkShaderModule m_FragmentShaderModule;
		std::string m_Name;
		uint32_t m_RendererID = 0;

		ShaderConfig m_Config;

		// 按 RenderPass 名缓存 Pipeline: "Mesh" → pipeline
		std::unordered_map<std::string, VkPipeline> m_PipelineCache;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_MaterialDescriptorSetLayout;
	};

	// ============================================================
	// 通用 Pipeline 创建 — 由 ShaderConfig 驱动
	// ============================================================
	class VulkanPipeline
	{
	public:
		// 根据 ShaderConfig 创建 Pipeline (配置驱动, 替代 CreateMeshPipeline / CreateGaussianPipeline)
		static VkPipeline Create(
			VkPipelineLayout& out_pipelineLayout,
			const VulkanShader& shader,
			const VkRenderPass& renderPass,
			const BufferLayout& bufferLayout,
			const VkDescriptorSetLayout& descriptorSetLayout
		);
	};

}
