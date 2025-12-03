#include "pch.h"
#include "Engine/Renderer/RendererAPI.h"

//#include "Platform/Vulkan/VulkanRendererAPI.h"

namespace Engine {

	RendererAPI::API RendererAPI::s_API = RendererAPI::API::Vulkan;

	//Scope<RendererAPI> RendererAPI::Create()
	//{
	//	switch (s_API)
	//	{
	//	case RendererAPI::API::None:    HZ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
	//	case RendererAPI::API::OpenGL:  return CreateScope<OpenGLRendererAPI>();
	//	}

	//	HZ_CORE_ASSERT(false, "Unknown RendererAPI!");
	//	return nullptr;
	//}

}