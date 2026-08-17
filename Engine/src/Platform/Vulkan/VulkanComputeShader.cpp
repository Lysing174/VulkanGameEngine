#include "pch.h"
#include "VulkanComputeShader.h"
#include "Platform/Vulkan/VulkanContext.h"

#include <fstream>

namespace Engine {

	VulkanComputeShader::VulkanComputeShader(const std::string& spvPath)
		: m_SpvPath(spvPath)
	{
		auto code = ReadFile(spvPath);
		m_ShaderModule = CreateShaderModule(code);
		EG_CORE_INFO("Created Vulkan Compute Shader Module from: {0}", spvPath);
	}

	VulkanComputeShader::~VulkanComputeShader()
	{
		DestroyResources();
	}

	VulkanComputeShader::VulkanComputeShader(VulkanComputeShader&& other) noexcept
		: m_ShaderModule(other.m_ShaderModule)
		, m_Pipeline(other.m_Pipeline)
		, m_SpvPath(std::move(other.m_SpvPath))
	{
		other.m_ShaderModule = VK_NULL_HANDLE;
		other.m_Pipeline = VK_NULL_HANDLE;
	}

	VulkanComputeShader& VulkanComputeShader::operator=(VulkanComputeShader&& other) noexcept
	{
		if (this != &other)
		{
			DestroyResources();
			m_ShaderModule = other.m_ShaderModule;
			m_Pipeline = other.m_Pipeline;
			m_SpvPath = std::move(other.m_SpvPath);
			other.m_ShaderModule = VK_NULL_HANDLE;
			other.m_Pipeline = VK_NULL_HANDLE;
		}
		return *this;
	}

	void VulkanComputeShader::CreatePipeline(VkPipelineLayout pipelineLayout)
	{
		// Destroy old pipeline if previously created
		DestroyPipeline();

		auto device = VulkanContext::Get()->GetDevice();

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.module = m_ShaderModule;
		stageInfo.pName = "main";

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.stage = stageInfo;
		pipelineInfo.layout = pipelineLayout;

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
		{
			EG_CORE_ERROR("Failed to create compute pipeline for: {0}", m_SpvPath);
			throw std::runtime_error("failed to create compute pipeline!");
		}
		EG_CORE_INFO("Created compute pipeline for: {0}", m_SpvPath);
	}

	void VulkanComputeShader::Dispatch(VkCommandBuffer cmd, VkPipelineLayout layout,
										VkDescriptorSet descSet, uint32_t x, uint32_t y, uint32_t z) const
	{
		EG_CORE_ASSERT(m_Pipeline != VK_NULL_HANDLE, "Dispatch called before CreatePipeline!");

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descSet, 0, nullptr);
		vkCmdDispatch(cmd, x, y, z);
	}

	VkShaderModule VulkanComputeShader::CreateShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		auto device = VulkanContext::Get()->GetDevice();
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			EG_CORE_ERROR("Failed to create compute shader module!");
			throw std::runtime_error("failed to create compute shader module!");
		}
		return shaderModule;
	}

	void VulkanComputeShader::DestroyPipeline()
	{
		if (m_Pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(VulkanContext::Get()->GetDevice(), m_Pipeline, nullptr);
			m_Pipeline = VK_NULL_HANDLE;
		}
	}

	void VulkanComputeShader::DestroyResources()
	{
		auto device = VulkanContext::Get()->GetDevice();
		DestroyPipeline();
		if (m_ShaderModule != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(device, m_ShaderModule, nullptr);
			m_ShaderModule = VK_NULL_HANDLE;
		}
	}

	std::vector<char> VulkanComputeShader::ReadFile(const std::string& filename)
	{
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open())
		{
			EG_CORE_ERROR("Failed to open shader file: {0}", filename);
			throw std::runtime_error("failed to open file!");
		}

		size_t fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}

} // namespace Engine
