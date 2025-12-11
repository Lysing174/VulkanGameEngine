#include "pch.h"
#include "Engine/Renderer/Shader.h"

#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanShader.h"

namespace Engine {
    std::shared_ptr<Shader> Shader::Create(const std::string& vertexSrc, const std::string& fragmentSrc)
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;

        case Renderer::API::Vulkan:  return std::make_shared<VulkanShader>(vertexSrc, fragmentSrc);
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

	std::shared_ptr<Shader> ShaderLibrary::Load(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		auto shader = Shader::Create(vertexSrc,fragmentSrc);
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
}