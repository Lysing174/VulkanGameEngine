#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"

namespace Engine {
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<MeshRenderCommandRequest> Renderer::s_MeshRenderQueue;
	std::vector<GaussianRenderCommandRequest> Renderer::s_GaussianRenderQueue;
    std::shared_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = std::make_shared<ShaderLibrary>();
    VkDescriptorSet Renderer::s_GaussianDescriptorSet = VK_NULL_HANDLE;

    void Renderer::Init()
    {
        // Mesh Shader: 使用默认 PBR 配置 (Albedo + Normal + ORM + MaterialUBO)
        s_ShaderLibrary->Load("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", ShaderConfig::DefaultPBR());

        // Gaussian Shader: 使用深度可视化配置 (仅 DepthMap 采样器 + Push Constants)
        s_ShaderLibrary->Load("shaders/Gaussian.vert.spv", "shaders/Gaussian.frag.spv", ShaderConfig::DepthVisualizer());

        // 创建 Gaussian 深度纹理 DescriptorSet
        CreateGaussianDescriptorSet();
    }

    void Renderer::CreateGaussianDescriptorSet()
    {
        auto gaussianShader = s_ShaderLibrary->Get("Gaussian.vert.spv");
        auto vkShader = std::static_pointer_cast<VulkanShader>(gaussianShader);
        auto context = VulkanContext::Get();

        // 如果已有旧的 descriptor set，先释放
        if (s_GaussianDescriptorSet != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(context->GetDevice(), context->GetDescriptorPool(), 1, &s_GaussianDescriptorSet);
            s_GaussianDescriptorSet = VK_NULL_HANDLE;
        }

        VkDescriptorSetLayout layout = vkShader->GetMaterialDescriptorSetLayout();
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        if (vkAllocateDescriptorSets(context->GetDevice(), &allocInfo, &s_GaussianDescriptorSet) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate Gaussian descriptor set!");
        }

        // 写入深度纹理
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = context->GetDepthImageView();
        imageInfo.sampler = context->GetDepthSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = s_GaussianDescriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    void Renderer::OnSwapchainRecreated()
    {
        CreateGaussianDescriptorSet();
    }

	void Renderer::BeginScene(const EditorCamera& camera)
	{
        s_MeshRenderQueue.clear();
        s_GaussianRenderQueue.clear();

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
		switch (Renderer::GetAPI())
		{
		case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
		case Renderer::API::Vulkan:
			{
				VulkanRenderer::BeginMeshRenderPass(); 
				FlushMeshPass();
				VulkanRenderer::EndRenderPass();
				// 切换到 Gaussian RenderPass
				VulkanRenderer::BeginGaussianRenderPass();
				FlushGaussianPass();
				// 注意: 不在这里 EndRenderPass / DrawImGui / EndFrame
				// 等 ImGui::Render() 生成 drawData 后再调用 PresentFrame()
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

	void Renderer::PresentFrame()
	{
		switch (Renderer::GetAPI())
		{
		case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
		case Renderer::API::Vulkan:
			{
				// ImGui 在 Gaussian RenderPass 内绘制 (覆盖在最上层)
				VulkanRenderer::DrawImGui();
				VulkanRenderer::EndRenderPass();
				VulkanRenderer::EndScene();
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

    void Renderer::SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID)
    {
        for (const auto& submesh : mesh->GetSubmeshes())
        {
            std::shared_ptr<Material> material = rendererComponent->GetMaterial(submesh.MaterialIndex);
            if (!material) { EG_CORE_WARN("Can't match material!"); continue; }

            glm::mat4 finalTransform = transform * submesh.Transform;

            MeshRenderCommandRequest request;
            request.Mesh = mesh;
            request.Material = material;
            request.Transform = finalTransform;
            request.EntityID = entityID;
            request.SubmeshIndexCount = submesh.IndexCount;
            request.SubmeshFirstIndex = submesh.FirstIndex;
            request.SubmeshFirstVertex = submesh.FirstVertex;

            s_MeshRenderQueue.push_back(request);
        }
    }

    void Renderer::SubmitGaussian(const glm::vec2& rectOffset, const glm::vec2& rectScale, int entityID)
    {
        GaussianRenderCommandRequest request;
        request.RectOffset = rectOffset;
        request.RectScale = rectScale;
        request.EntityID = entityID;

        s_GaussianRenderQueue.push_back(request);
    }

    void Renderer::FlushMeshPass()
    {
        if (s_MeshRenderQueue.empty()) return;

        // ============================================================
        // 排序策略: 按 Vulkan 状态切换成本从高到低排序
        //   1. Shader  (vkCmdBindPipeline)       — 成本最高, 排序主键
        //   2. Material (vkCmdBindDescriptorSets) — 成本中等, 排序次键
        // 最小化 Pipeline 和 DescriptorSet 的切换次数.
        // ============================================================
        std::sort(s_MeshRenderQueue.begin(), s_MeshRenderQueue.end(),
            [](const MeshRenderCommandRequest& a, const MeshRenderCommandRequest& b) {
                uint32_t shaderA = a.Material->GetShader()->GetRendererID();
                uint32_t shaderB = b.Material->GetShader()->GetRendererID();
                if (shaderA != shaderB) return shaderA < shaderB;
                return a.Material->GetRendererID() < b.Material->GetRendererID();
            });

        // 遍历并渲染 — 只在状态实际变化时才切换
        std::shared_ptr<Shader>   lastShader   = nullptr;
        std::shared_ptr<Material> lastMaterial  = nullptr;

        for (auto& command : s_MeshRenderQueue)
        {
            // ① 切换 Pipeline (最贵) — 仅当 Shader 变化时
            if (command.Material->GetShader() != lastShader)
            {
                lastShader = command.Material->GetShader();
                lastShader->Bind();  // → vkCmdBindPipeline

                // Pipeline 切换后, DescriptorSet 绑定状态失效, 必须重绑 Material
                lastMaterial = nullptr;
            }

            // ② 切换 DescriptorSet (中等) — 仅当 Material 变化时
            if (command.Material != lastMaterial)
            {
                lastMaterial = command.Material;
                lastMaterial->Bind();  // → vkCmdBindDescriptorSets + PushColor
            }
            DrawMesh(command);
        }
    }

    void Renderer::FlushGaussianPass()
    {
        if (s_GaussianRenderQueue.empty()) return;

        auto gaussianShader = s_ShaderLibrary->Get("Gaussian.vert.spv");
        gaussianShader->Bind();  // vkCmdBindPipeline

        VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

        // 绑定 Global DescriptorSet (set=0)
        VkDescriptorSet globalDescriptorSet = VulkanContext::Get()->GetCurrentDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            gaussianShader->GetPipelineLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

        // 绑定深度纹理 DescriptorSet (set=1, 直接绑定，不走 Material)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            gaussianShader->GetPipelineLayout(), 1, 1, &s_GaussianDescriptorSet, 0, nullptr);

        for (auto& command : s_GaussianRenderQueue)
        {
            DrawGaussian(command);
        }
    }
	
	void Renderer::DrawMesh(const MeshRenderCommandRequest& request)
    {
    	switch (GetAPI())
    	{
    	case API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
    	case API::Vulkan:  VulkanRenderer::DrawMesh(request); return;
    	}

    	EG_CORE_ASSERT(false, "Unknown RendererAPI!");
    	return;

    }

	void Renderer::DrawGaussian(const GaussianRenderCommandRequest& request)
    {
    	switch (GetAPI())
    	{
    	case API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
    	case API::Vulkan:  VulkanRenderer::DrawGaussian(request); return;
    	}

    	EG_CORE_ASSERT(false, "Unknown RendererAPI!");
    	return;
    }
	
}
