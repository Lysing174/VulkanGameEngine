#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanFrameBuffer.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<MeshRenderCommandRequest> Renderer::s_MeshRenderQueue;
    std::shared_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = std::make_shared<ShaderLibrary>();
    std::shared_ptr<Framebuffer> Renderer::s_OffscreenFramebuffer = nullptr;
    Renderer::SceneData Renderer::s_SceneData;

    // Light SSBO
    VkBuffer Renderer::s_LightSSBO = VK_NULL_HANDLE;
    VkDeviceMemory Renderer::s_LightSSBOMemory = VK_NULL_HANDLE;
    std::vector<GPULight> Renderer::s_SceneLights;
    bool Renderer::s_LightsDirty = true;
    uint32_t Renderer::s_SceneLightCount = 0;

    // Skybox
    std::shared_ptr<Shader> Renderer::s_SkyboxShader = nullptr;
    std::shared_ptr<Mesh>   Renderer::s_SkyboxMesh   = nullptr;

	// ============================================================
    // Init
    // ============================================================
    void Renderer::Init()
    {
        // Mesh Shader: PBR
        s_ShaderLibrary->Load("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", ShaderConfig::DefaultPBR());
        // Skybox Shader
        s_ShaderLibrary->Load("shaders/Skybox.vert.spv", "shaders/Skybox.frag.spv", ShaderConfig::Skybox());
    }

    // ============================================================
    // OnSwapchainRecreated
    // ============================================================
    void Renderer::OnSwapchainRecreated()
    {
        // 重新缓存 light SSBO 句柄 (swapchain 重建时 VulkanContext 会重建 buffer)
        s_LightSSBO = VK_NULL_HANDLE;
        s_LightSSBOMemory = VK_NULL_HANDLE;
    }

    // ============================================================
    // Scene lifecycle
    // ============================================================
	void Renderer::BeginScene(const EditorCamera& camera)
	{
        s_MeshRenderQueue.clear();

        s_SceneData.CameraPosition = camera.GetPosition();
        s_SceneData.CameraForward  = camera.GetForwardDirection();
        s_SceneData.ViewMatrix     = camera.GetViewMatrix();
        s_SceneData.ProjectionMatrix = camera.GetProjection();
        s_SceneData.ProjectionMatrix[1][1] *= -1; // Vulkan Y-flip

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
				// 上传光照 SSBO
				EndLightCollection();

				// 更新相机位置到 UBO
				{
					auto context = VulkanContext::Get();
					glm::vec4 cameraPos(s_SceneData.CameraPosition, 0.0f);
					context->UpdateGlobalCameraUniforms(cameraPos);
				}

				VulkanRenderer::BeginMeshRenderPass(
					std::static_pointer_cast<VulkanFramebuffer>(s_OffscreenFramebuffer));

				// Draw skybox first (behind all meshes, depth write off)
				DrawSkybox();

				FlushMeshPass();
				VulkanRenderer::EndRenderPass();  // Close mesh RP
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
				VulkanRenderer::BeginUIRenderPass();  // Open ImGui RP (LOAD, not CLEAR)
				VulkanRenderer::DrawImGui();           // Draw ImGui in its own RP
				VulkanRenderer::EndRenderPass();        // Close ImGui RP
				VulkanRenderer::EndScene();             // Submit command buffer
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

    void Renderer::SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID)
    {
        // 从模型矩阵获取世界位置
        glm::vec3 worldPos = glm::vec3(transform[3]);

        // CPU 光源裁剪: 选出最近的 N 个光源
        uint32_t lightCount = 0;
        uint32_t lightIndices[MAX_LIGHTS_PER_OBJECT] = {};
        CullLightsForObject(worldPos, rendererComponent->MaxLights, lightCount, lightIndices);

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

            // 存储光源裁剪结果
            request.LightCount = lightCount;
            memcpy(request.LightIndices, lightIndices, sizeof(lightIndices));

            s_MeshRenderQueue.push_back(request);
        }
    }
    

    // ============================================================
    // Light System
    // ============================================================
    void Renderer::CreateLightBuffer()
    {
        // Light SSBO 由 VulkanContext::createDescriptorSets() 创建和管理
        // 这里缓存其句柄供 Renderer 使用
        auto context = VulkanContext::Get();
        s_LightSSBO = context->GetLightSSBO();
        s_LightSSBOMemory = context->GetLightSSBOMemory();
    }

    void Renderer::UpdateLightBuffer()
    {
        if (!s_LightsDirty) return;

        auto context = VulkanContext::Get();
        auto device = context->GetDevice();

        // 确保 Light SSBO 已创建
        if (s_LightSSBO == VK_NULL_HANDLE)
        {
            CreateLightBuffer();
        }

        VkDeviceSize bufferSize = context->GetLightSSBOSize();

        // 填充到固定大小以匹配 SSBO
        std::vector<GPULight> gpuLights(MAX_LIGHTS);
        memset(gpuLights.data(), 0, MAX_LIGHTS * sizeof(GPULight));
        uint32_t count = std::min(s_SceneLightCount, MAX_LIGHTS);
        memcpy(gpuLights.data(), s_SceneLights.data(), count * sizeof(GPULight));

        void* data;
        vkMapMemory(device, s_LightSSBOMemory, 0, bufferSize, 0, &data);
        memcpy(data, gpuLights.data(), bufferSize);
        vkUnmapMemory(device, s_LightSSBOMemory);

        s_LightsDirty = false;
    }

    void Renderer::BeginLightCollection()
    {
        s_SceneLights.clear();
        s_LightsDirty = true;
    }

    void Renderer::SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range)
    {
        if (s_SceneLights.size() >= MAX_LIGHTS) return;

        GPULight light;
        light.PositionType   = glm::vec4(position, 0.0f);  // w=0: 点光
        light.ColorIntensity = glm::vec4(color, intensity);
        light.DirectionRange = glm::vec4(0.0f, 0.0f, 0.0f, range);
        s_SceneLights.push_back(light);
    }

    void Renderer::SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity)
    {
        if (s_SceneLights.size() >= MAX_LIGHTS) return;

        GPULight light;
        light.PositionType   = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // w=1: 方向光
        light.ColorIntensity = glm::vec4(color, intensity);
        light.DirectionRange = glm::vec4(glm::normalize(direction), 0.0f);
        s_SceneLights.push_back(light);
    }

    void Renderer::EndLightCollection()
    {
        s_SceneLightCount = (uint32_t)s_SceneLights.size();

        // 首次创建 SSBO
        if (s_LightSSBO == VK_NULL_HANDLE)
            CreateLightBuffer();

        UpdateLightBuffer();
    }

    void Renderer::CullLightsForObject(const glm::vec3& worldPos, uint32_t maxLights,
                                       uint32_t& outCount, uint32_t* outIndices)
    {
        outCount = 0;
        if (s_SceneLightCount == 0) return;

        uint32_t actualMax = std::min(maxLights, MAX_LIGHTS_PER_OBJECT);

        // 计算与每个光源的距离，存储 (index, distance) 对
        struct LightDist { uint32_t index; float dist; };
        std::vector<LightDist> distances;
        distances.reserve(s_SceneLightCount);

        for (uint32_t i = 0; i < s_SceneLightCount; i++)
        {
            float dist = 0.0f;
            if (s_SceneLights[i].PositionType.w < 0.5f) // 点光源: w=0
            {
                glm::vec3 lightPos = glm::vec3(s_SceneLights[i].PositionType);
                dist = glm::distance(worldPos, lightPos);
            }
            else // 方向光: w=1, 距离为零 (总是最近)
            {
                dist = -1.0f; // 方向光永远优先
            }
            distances.push_back({ i, dist });
        }

        // 按距离排序 (方向光在前)
        std::sort(distances.begin(), distances.end(),
            [](const LightDist& a, const LightDist& b) { return a.dist < b.dist; });

        // 取前 N 个
        for (uint32_t i = 0; i < actualMax && i < s_SceneLightCount; i++)
        {
            outIndices[i] = distances[i].index;
            outCount++;
        }
    }

    void Renderer::FlushMeshPass()
    {
        if (s_MeshRenderQueue.empty()) return;

        std::sort(s_MeshRenderQueue.begin(), s_MeshRenderQueue.end(),
            [](const MeshRenderCommandRequest& a, const MeshRenderCommandRequest& b) {
                uint32_t shaderA = a.Material->GetShader()->GetRendererID();
                uint32_t shaderB = b.Material->GetShader()->GetRendererID();
                if (shaderA != shaderB) return shaderA < shaderB;
                return a.Material->GetRendererID() < b.Material->GetRendererID();
            });

        std::shared_ptr<Shader>   lastShader   = nullptr;
        std::shared_ptr<Material> lastMaterial  = nullptr;

        for (auto& command : s_MeshRenderQueue)
        {
            if (command.Material->GetShader() != lastShader)
            {
                lastShader = command.Material->GetShader();
                lastShader->Bind();
                lastMaterial = nullptr;
            }

            if (command.Material != lastMaterial)
            {
                lastMaterial = command.Material;
                lastMaterial->Bind();
            }
            DrawMesh(command);
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

	void Renderer::SetSkyboxData(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh)
	{
		s_SkyboxShader = shader;
		s_SkyboxMesh   = mesh;
	}

	void Renderer::DrawSkybox()
	{
		if (!s_SkyboxShader || !s_SkyboxMesh)
			return;

		auto* ctx = VulkanContext::Get();
		VkCommandBuffer cmd = ctx->GetCurrentCommandBuffer();

		s_SkyboxShader->Bind();

		// Remove translation from view matrix for infinite skybox
		glm::mat4 viewNoTrans = s_SceneData.ViewMatrix;
		viewNoTrans[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 vp = s_SceneData.ProjectionMatrix * viewNoTrans;

		VkPipelineLayout layout = s_SkyboxShader->GetPipelineLayout();
		vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);

		// Bind global descriptor set (set=0) which contains the cubemap at binding 3
		VkDescriptorSet globalSet = ctx->GetCurrentDescriptorSet();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			layout, 0, 1, &globalSet, 0, nullptr);

		s_SkyboxMesh->Bind();
		const auto& sub = s_SkyboxMesh->GetSubmeshes()[0];
		vkCmdDrawIndexed(cmd, sub.IndexCount, 1, sub.FirstIndex, sub.FirstVertex, 0);
	}

	
}
