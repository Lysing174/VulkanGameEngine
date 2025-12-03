#include "pch.h"
#include "Engine/Renderer/Shader.h"

#include "Engine/Renderer/RendererAPI.h"
#include "Platform/Vulkan/VulkanShader.h"

namespace Engine {
    std::shared_ptr<Shader> Shader::Create(const std::string& vertexSrc, const std::string& fragmentSrc)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;

        case RendererAPI::API::Vulkan:  return std::make_shared<VulkanShader>(vertexSrc, fragmentSrc);
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
}