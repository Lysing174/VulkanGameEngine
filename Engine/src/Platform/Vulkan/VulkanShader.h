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
		VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

		// --- 暂时废弃的 OpenGL 接口 (Vulkan 不这么做) ---
		// 在 Vulkan 中，这些操作通过 UniformBuffer 和 DescriptorSet 完成
		// 这里留空或者是打印警告
		virtual void SetInt(const std::string& name, int value) override {}
		virtual void SetFloat3(const std::string& name, const glm::vec3& value) override {}
		virtual void SetMat4(const std::string& name, const glm::mat4& value) override {}

		virtual const std::string& GetName() const override { return m_Name; }

	private:
		static std::vector<char> ReadFile(const std::string& filename);
		VkShaderModule CreateShaderModule(const std::vector<char>& code);


	private:
		VkShaderModule m_VertexShaderModule;
		VkShaderModule m_FragmentShaderModule;
		std::string m_Name;
		VkPipeline m_GraphicsPipeline;
		VkPipelineLayout m_PipelineLayout;
	};

}