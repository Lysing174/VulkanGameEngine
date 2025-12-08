#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"

namespace Engine {
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<RenderCommandRequest> Renderer::s_RenderQueue;

	void Renderer::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
        s_RenderQueue.clear();

        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  VulkanRenderer::BeginScene(camera, transform); return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;
	}

	void Renderer::BeginScene(const EditorCamera& camera)
	{
        s_RenderQueue.clear();

        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  VulkanRenderer::BeginScene(camera); return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;
	}

	void Renderer::EndScene()
	{
		Flush();
        switch (Renderer::GetAPI())
        {
        case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case Renderer::API::Vulkan:  VulkanRenderer::EndScene(); return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;

	}

	void Renderer::DrawMesh(RenderCommandRequest request)
	{
        switch (GetAPI())
        {
        case API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
        case API::Vulkan:  VulkanRenderer::DrawMesh(request); return;
        }

        EG_CORE_ASSERT(false, "Unknown RendererAPI!");
        return;

	}
    void Renderer::SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID)
    {
        for (const auto& submesh : mesh->GetSubmeshes())
        {
            std::shared_ptr<Material> material = rendererComponent->GetMaterial(submesh.MaterialIndex);
            if (!material) { EG_CORE_WARN("Can't match material!"); continue; }

            glm::mat4 finalTransform = transform * submesh.Transform;

            RenderCommandRequest request;
            request.Mesh = mesh;
            request.Material = material;
            request.Transform = finalTransform;
            request.EntityID = entityID;
            request.SubmeshIndexCount = submesh.IndexCount;
            request.SubmeshFirstIndex = submesh.FirstIndex;
            request.SubmeshFirstVertex = submesh.FirstVertex;

            s_RenderQueue.push_back(request);
        }
    }

    void Renderer::Flush()
    {
        // 按照shader的指针地址进行排序
        std::sort(s_RenderQueue.begin(), s_RenderQueue.end(),
            [](const RenderCommandRequest& a, const RenderCommandRequest& b) {
                uint32_t shaderA = a.Material->GetShader()->GetRendererID();
                uint32_t shaderB = b.Material->GetShader()->GetRendererID();
                if (shaderA != shaderB) return shaderA < shaderB;
                return a.Material->GetRendererID() < b.Material->GetRendererID();
            });

        // 遍历并渲染
        std::shared_ptr<Shader> lastShader = nullptr;
        glm::vec4 lastColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        std::shared_ptr<Material> lastMaterial = nullptr;

        for (auto& command : s_RenderQueue)
        {
            if (command.Material->GetShader() != lastShader)
            {
                lastShader = command.Material->GetShader();
                lastShader->Bind(); 
            }
            if (command.Material->GetColor() != lastColor) 
            {
                lastColor = command.Material->GetColor();
                command.Material->PushColor(lastColor);
            }
            if (command.Material != lastMaterial)
            {
                lastMaterial = command.Material;
                lastMaterial->Bind(); 
            }

            DrawMesh(command);
        }
    }
}