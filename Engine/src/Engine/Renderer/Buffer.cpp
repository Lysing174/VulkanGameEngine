#include "pch.h"
#include "Engine/Renderer/Buffer.h"

#include "Engine/Renderer/RendererAPI.h"

#include "Platform/Vulkan/VulkanBuffer.h"

namespace Engine {
    VertexBuffer* VertexBuffer::Create(float* vertices, uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case RendererAPI::API::Vulkan:  return new VulkanVertexBuffer(vertices, size);
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
    IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t size)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
        case RendererAPI::API::Vulkan:  return new VulkanIndexBuffer(indices, size);
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}