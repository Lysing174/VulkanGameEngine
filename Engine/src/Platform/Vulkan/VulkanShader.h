#pragma once

#include <Engine/Renderer/Shader.h>
#include <Platform/Vulkan/VulkanBuffer.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace Engine {

	class VulkanShader : public Shader
	{
	public:
		VulkanShader(const std::string& vertexSrc, const std::string& fragmentSrc);
		virtual ~VulkanShader();

		virtual void Bind() const override;
		virtual void Unbind() const override;

		virtual void CreatePipeline(const BufferLayout& layout) override;

		VkShaderModule GetVertexShaderModule() const { return m_VertexShaderModule; }
		VkShaderModule GetFragmentShaderModule() const { return m_FragmentShaderModule; }
		VkPipeline GetPipeline() const { return m_GraphicsPipeline; }

		//virtual void SetInt(const std::string& name, int value) override {}
		//virtual void SetFloat3(const std::string& name, const glm::vec3& value) override {}
		//virtual void SetMat4(const std::string& name, const glm::mat4& value) override {}

		virtual const std::string& GetName() const override { return m_Name; }
		virtual uint32_t GetRendererID() const override { return m_RendererID; }
		virtual const VkPipelineLayout GetPipelineLayout() const override { return m_PipelineLayout; }
		// 获取 DescriptorSetLayout (创建 Material 时需要)
		VkDescriptorSetLayout GetMaterialDescriptorSetLayout() const { return m_MaterialDescriptorSetLayout; }


	private:
		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code);
		void CreateDescriptorSetLayout();


	private:
		VkShaderModule m_VertexShaderModule;
		VkShaderModule m_FragmentShaderModule;
		std::string m_Name;
		uint32_t m_RendererID = 0;
		VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_MaterialDescriptorSetLayout;
	};

}