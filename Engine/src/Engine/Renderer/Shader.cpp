#include "pch.h"
#include "Engine/Renderer/Shader.h"

#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanComputeShader.h"

namespace Engine {
    std::shared_ptr<Shader> Shader::Create(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config)
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;

        case Renderer::API::Vulkan:  return std::make_shared<VulkanShader>(vertexSrc, fragmentSrc, config);
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
	void Shader::Bind() const
	{

	}
	void Shader::Unbind() const
	{
	}


	void ShaderLibrary::Add(const std::shared_ptr<Shader>& shader)
	{
		auto& name = shader->GetName();
		EG_CORE_ASSERT(!Exists(name), "Shader already exists!");
		m_Shaders[name] = shader;
	}

	std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& vertexSrc, const std::string& fragmentSrc, const ShaderConfig& config)
	{
		auto shader = Shader::Create(vertexSrc, fragmentSrc, config);
		Add(shader);
		return shader;
	}


	std::shared_ptr<Shader> ShaderLibrary::Get(const std::string& name)
	{
		EG_CORE_ASSERT(Exists(name), "Shader not found!");
		return m_Shaders[name];
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		return m_Shaders.find(name) != m_Shaders.end();
	}

	// ---- 计算着色器 (区别于 VS/FS) ----
	std::shared_ptr<VulkanComputeShader> ShaderLibrary::LoadCompute(const std::string& spvPath, const ShaderConfig& config)
	{
		// 以 .spv 路径的 basename 作为库内名称 (与 VulkanShader 一致)
		size_t found = spvPath.find_last_of("/\\");
		std::string name = spvPath.substr(found + 1);
		EG_CORE_ASSERT(!ComputeExists(name), "Compute shader already exists!");

		auto shader = std::make_shared<VulkanComputeShader>(spvPath, config);
		m_ComputeShaders[name] = shader;
		return shader;
	}

	std::shared_ptr<VulkanComputeShader> ShaderLibrary::GetCompute(const std::string& name)
	{
		EG_CORE_ASSERT(ComputeExists(name), "Compute shader not found!");
		return m_ComputeShaders[name];
	}

	bool ShaderLibrary::ComputeExists(const std::string& name) const
	{
		return m_ComputeShaders.find(name) != m_ComputeShaders.end();
	}
}
