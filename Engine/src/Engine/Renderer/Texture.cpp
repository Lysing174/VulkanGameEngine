#include "pch.h"
#include "Texture.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanTexture.h"
namespace Engine
{
    std::shared_ptr<Texture2D> Texture2D::s_WhiteTexture=nullptr;
    std::shared_ptr<Texture2D> Texture2D::s_BlueTexture = nullptr;

    std::shared_ptr<Texture2D> Texture2D::GetWhiteTexture()
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::Vulkan:
            if (!s_WhiteTexture) 
            {
                uint32_t data = 0xffffffff; // 纯白 RGBA
                s_WhiteTexture = std::make_shared<VulkanTexture2D>(1, 1, &data);
            }
            return s_WhiteTexture;
        }
        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::GetBlueTexture()
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::Vulkan:
            if (!s_BlueTexture)
            {
                uint32_t data = 0xffff8080; 
                s_BlueTexture = std::make_shared<VulkanTexture2D>(1, 1, &data);
            }
            return s_BlueTexture;
        }
        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path)
    {
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case Renderer::API::Vulkan:
            return std::make_shared<VulkanTexture2D>(VulkanTexture2D(path));
        }
        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
