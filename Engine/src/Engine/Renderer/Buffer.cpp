#include "pch.h"
#include "Engine/Renderer/Buffer.h"

#include "Engine/Renderer/RendererAPI.h"

#include "Platform/Vulkan/VulkanBuffer.h"

namespace Engine {
    std::shared_ptr<VertexBuffer> VertexBuffer::Create(std::vector<Vertex> vertices, uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case RendererAPI::API::Vulkan:  return std::make_shared<VulkanVertexBuffer>(vertices, size);
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
    std::shared_ptr<IndexBuffer> IndexBuffer::Create(std::vector<uint32_t> indices, uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case RendererAPI::API::Vulkan:  return std::make_shared<VulkanIndexBuffer>(indices, size);
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}