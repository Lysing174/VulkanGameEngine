#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanBuffer.h"

namespace Engine {
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<MeshRenderCommandRequest> Renderer::s_MeshRenderQueue;
	std::vector<GaussianRenderCommandRequest> Renderer::s_GaussianRenderQueue;
    std::shared_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = std::make_shared<ShaderLibrary>();
    Renderer::SceneData Renderer::s_SceneData;

    // Global Gaussian SSBO
    std::shared_ptr<ShaderStorageBuffer> Renderer::s_GlobalGaussianSSBO;
    uint32_t Renderer::s_TotalGaussianCount = 0;
    bool Renderer::s_GaussianSSBODirty = false;
    std::vector<std::shared_ptr<GaussianModel>> Renderer::s_RegisteredGaussianModels;
    VkDescriptorSet Renderer::s_GaussianDescriptorSet = VK_NULL_HANDLE;

    void Renderer::Init()
    {
        // Mesh Shader: 使用默认 PBR 配置 (Albedo + Normal + ORM + MaterialUBO)
        s_ShaderLibrary->Load("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", ShaderConfig::DefaultPBR());

        // Gaussian Shader: 使用高斯点云配置 (深度采样器 + GaussianData SSBO + POINT_LIST)
        s_ShaderLibrary->Load("shaders/Gaussian.vert.spv", "shaders/Gaussian.frag.spv", ShaderConfig::GaussianPointCloud());
    }

    void Renderer::RegisterGaussianModel(const std::shared_ptr<GaussianModel>& model)
    {
        if (!model || model->IsRegistered()) return;

        // Check if already in the list (by raw pointer)
        for (const auto& registered : s_RegisteredGaussianModels)
        {
            if (registered.get() == model.get()) return;
        }

        // Assign offset and add to list
        model->SetGlobalOffset(s_TotalGaussianCount);
        s_TotalGaussianCount += model->GetGaussianCount();
        s_RegisteredGaussianModels.push_back(model);
        s_GaussianSSBODirty = true;

        EG_CORE_INFO("Registered GaussianModel: offset={0}, count={1}, total={2}",
            model->GetGlobalOffset(), model->GetGaussianCount(), s_TotalGaussianCount);
    }

    void Renderer::RebuildGlobalGaussianSSBO()
    {
        if (s_RegisteredGaussianModels.empty()) return;

        auto context = VulkanContext::Get();

        // Collect all GPU data from all registered models
        std::vector<GaussianDataGPU> allData;
        allData.reserve(s_TotalGaussianCount);
        for (const auto& model : s_RegisteredGaussianModels)
        {
            auto gpuData = model->GetGPUData();
            allData.insert(allData.end(), gpuData.begin(), gpuData.end());
        }

        // Create new SSBO with all data
        s_GlobalGaussianSSBO = ShaderStorageBuffer::Create(
            allData.data(), (uint32_t)(allData.size() * sizeof(GaussianDataGPU)));

        // Update descriptor set with new SSBO + depth texture
        CreateGaussianDescriptorSet();

        s_GaussianSSBODirty = false;

        EG_CORE_INFO("Rebuilt global Gaussian SSBO: {0} gaussians, {1} bytes",
            allData.size(), allData.size() * sizeof(GaussianDataGPU));
    }

    void Renderer::CreateGaussianDescriptorSet()
    {
        auto gaussianShader = s_ShaderLibrary->Get("Gaussian.vert.spv");
        auto vkShader = std::static_pointer_cast<VulkanShader>(gaussianShader);
        auto context = VulkanContext::Get();

        // Free old descriptor set if exists
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

        // Write depth texture (binding 0)
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo.imageView = context->GetDepthImageView();
        imageInfo.sampler = context->GetDepthSampler();

        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = s_GaussianDescriptorSet;
        depthWrite.dstBinding = 0;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthWrite.descriptorCount = 1;
        depthWrite.pImageInfo = &imageInfo;

        std::vector<VkWriteDescriptorSet> writes = { depthWrite };

        // Write SSBO (binding 1) if it exists
        VkDescriptorBufferInfo bufferInfo{};
        VkWriteDescriptorSet ssboWrite{};
        if (s_GlobalGaussianSSBO)
        {
            auto vkSSBO = std::static_pointer_cast<VulkanShaderStorageBuffer>(s_GlobalGaussianSSBO);
            bufferInfo.buffer = vkSSBO->GetVulkanBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = s_TotalGaussianCount * sizeof(GaussianDataGPU);

            ssboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            ssboWrite.dstSet = s_GaussianDescriptorSet;
            ssboWrite.dstBinding = 1;
            ssboWrite.dstArrayElement = 0;
            ssboWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ssboWrite.descriptorCount = 1;
            ssboWrite.pBufferInfo = &bufferInfo;

            writes.push_back(ssboWrite);
        }

        vkUpdateDescriptorSets(context->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }

    void Renderer::OnSwapchainRecreated()
    {
        CreateGaussianDescriptorSet();
    }

	void Renderer::BeginScene(const EditorCamera& camera)
	{
        s_MeshRenderQueue.clear();
        s_GaussianRenderQueue.clear();

        s_SceneData.CameraPosition = camera.GetPosition();
        s_SceneData.CameraForward  = camera.GetForwardDirection();

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

    void Renderer::SubmitGaussian(const glm::mat4& transform, const std::shared_ptr<GaussianModel>& model, int entityID)
    {
        GaussianRenderCommandRequest request;
        request.Transform = transform;
        request.Model = model;
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

        // Rebuild global SSBO if dirty (new model registered since last rebuild)
        if (s_GaussianSSBODirty)
            RebuildGlobalGaussianSSBO();

        if (!s_GlobalGaussianSSBO) return;

        auto gaussianShader = s_ShaderLibrary->Get("Gaussian.vert.spv");
        gaussianShader->Bind();  // vkCmdBindPipeline

        VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();
        auto context = VulkanContext::Get();

        // 绑定 Global DescriptorSet (set=0): projView UBO
        VkDescriptorSet globalDescriptorSet = context->GetCurrentDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            gaussianShader->GetPipelineLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

        // 绑定 Gaussian DescriptorSet (set=1): depth texture + global SSBO — 只需绑一次
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            gaussianShader->GetPipelineLayout(), 1, 1, &s_GaussianDescriptorSet, 0, nullptr);

        // 绘制每个 GaussianModel (各自 push transform + 用 offset 绘制)
        for (auto& command : s_GaussianRenderQueue)
        {
            if (!command.Model) continue;
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
