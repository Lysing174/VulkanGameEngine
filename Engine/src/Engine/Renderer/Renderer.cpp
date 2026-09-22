#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanFrameBuffer.h"

#include <algorithm>
#include <cfloat>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>


namespace Engine {
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<MeshRenderCommandRequest> Renderer::s_MeshRenderQueue;
    std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = std::make_unique<ShaderLibrary>();
    std::unique_ptr<ModelLibrary> Renderer::s_ModelLibrary = std::make_unique<ModelLibrary>();
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

    // Shadow
    Renderer::ShadowLight Renderer::s_ShadowLights[MAX_SHADOW_MAPS];
    uint32_t              Renderer::s_ShadowLightCount = 0;
    glm::mat4             Renderer::s_ShadowViewProjs[MAX_SHADOW_MAPS];        // 采样用 (含 bias)
    glm::mat4             Renderer::s_ShadowRenderViewProjs[MAX_SHADOW_MAPS];  // 渲染用 (无 bias)

	// ============================================================
    // Init
    // ============================================================
    void Renderer::Init()
    {
        // Mesh Shader: PBR
        s_ShaderLibrary->Load("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", ShaderConfig::DefaultPBR());
        // Skybox Shader
        s_ShaderLibrary->Load("shaders/Skybox.vert.spv", "shaders/Skybox.frag.spv", ShaderConfig::Skybox());
        // Shadow Shader (方向光阴影贴图生成 pass)
        s_ShaderLibrary->Load("shaders/Shadow.vert.spv", "shaders/Shadow.frag.spv", ShaderConfig::Shadow());
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

	void Renderer::RunMeshPass()
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

				// 绑定 global descriptor set：swapchain/descriptor 重建期间可能为 VK_NULL_HANDLE，
				// 此时直接跳过绘制 (避免 VUID-vkCmdBindDescriptorSets-None-08600)
				VkDescriptorSet globalSet = VulkanContext::Get()->GetCurrentDescriptorSet();
				if (globalSet == VK_NULL_HANDLE)
				{
					VulkanRenderer::EndRenderPass();  // Close mesh RP (nothing drawn)
					break;
				}

				// Draw skybox first (behind all meshes, depth write off)
				DrawSkybox();

				DrawMeshes();
				VulkanRenderer::EndRenderPass();  // Close mesh RP
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

	void Renderer::RunImGuiPass()
	{
		switch (Renderer::GetAPI())
		{
		case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
		case Renderer::API::Vulkan:
			{
				VulkanRenderer::BeginUIRenderPass();  // Open ImGui RP (LOAD, not CLEAR)
				VulkanRenderer::DrawImGui();           // Draw ImGui in its own RP
				VulkanRenderer::EndRenderPass();        // Close ImGui RP
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

	void Renderer::EndScene()
	{
		switch (Renderer::GetAPI())
		{
		case Renderer::API::None:    EG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return;
		case Renderer::API::Vulkan:
			{
				VulkanRenderer::EndScene();  // 提交命令缓冲并呈现
				break;
			}
		default: EG_CORE_ASSERT(false, "Unknown RendererAPI!"); return;
		}
	}

    void Renderer::SubmitMesh(const glm::mat4& transform, const UUID& meshID, const MeshRendererComponent& rendererComponent, int entityID)
    {
        // 由 Renderer 从 ModelLibrary 按 UUID 解析出 Mesh 指针
        auto meshModel = Renderer::GetModelLibrary()->Load(meshID);
        auto mesh = meshModel ? meshModel->GetMesh() : nullptr;
        if (!mesh) { return; }

        // 材质来源 model (可能与 mesh 不同，支持跨模型自由组合)
        auto matModel = Renderer::GetModelLibrary()->Load(rendererComponent.MaterialID);

        // 从模型矩阵获取世界位置
        glm::vec3 worldPos = glm::vec3(transform[3]);

        // CPU 光源裁剪: 选出最近的 N 个光源
        uint32_t lightCount = 0;
        uint32_t lightIndices[MAX_LIGHTS_PER_OBJECT] = {};
        CullLightsForObject(worldPos, rendererComponent.MaxLights, lightCount, lightIndices);

        for (const auto& submesh : mesh->GetSubmeshes())
        {
            std::shared_ptr<Material> material = nullptr;
            if (matModel)
            {
                const auto& materials = matModel->GetMaterials();
                if (submesh.MaterialIndex < materials.size())
                    material = materials[submesh.MaterialIndex];
            }
            if (!material) { continue; }

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

    uint32_t Renderer::SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity)
    {
        if (s_SceneLights.size() >= MAX_LIGHTS) return 0xFFFFFFFF;

        GPULight light;
        light.PositionType   = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // w=1: 方向光
        light.ColorIntensity = glm::vec4(color, intensity);
        light.DirectionRange = glm::vec4(glm::normalize(direction), 0.0f);
        uint32_t idx = (uint32_t)s_SceneLights.size();
        s_SceneLights.push_back(light);
        return idx;
    }

    void Renderer::EndLightCollection()
    {
        s_SceneLightCount = (uint32_t)s_SceneLights.size();

        // 首次创建 SSBO
        if (s_LightSSBO == VK_NULL_HANDLE)
            CreateLightBuffer();

        UpdateLightBuffer();
    }

    // ============================================================
    // Shadow System
    // ============================================================
    void Renderer::BeginShadowCollection()
    {
        s_ShadowLightCount = 0;
    }

    void Renderer::SubmitShadowLight(const glm::vec3& position, const glm::vec3& direction,
                                     const glm::vec3& halfExtents, float shadowBias, uint32_t lightIndex)
    {
        if (s_ShadowLightCount >= MAX_SHADOW_MAPS)
        {
            EG_CORE_WARN("Shadow light count exceeds MAX_SHADOW_MAPS ({0}), extra lights ignored", MAX_SHADOW_MAPS);
            return;
        }
        auto& sl = s_ShadowLights[s_ShadowLightCount++];
        sl.Position    = position;
        sl.Direction   = glm::normalize(direction);
        sl.HalfExtents = halfExtents;
        sl.ShadowBias  = shadowBias;
        sl.LightIndex  = lightIndex;
    }

    // 依据各阴影光的范围盒 (位置=近端面, +Z=光照方向, Scale=半长) 计算光视矩阵
    // 约定与主相机一致: 右手坐标系, lookAt 向前=-z, 仅翻 Y 不翻 Z, 深度 NDC∈[-1,1]
    void Renderer::EndShadowCollection()
    {
        for (uint32_t s = 0; s < s_ShadowLightCount; s++)
        {
            const auto& sl = s_ShadowLights[s];
            glm::vec3 lightDir = sl.Direction;     // 光传播方向 (世界空间, 指向被照亮处 = 本地 +Z)
            glm::vec3 lightPos = sl.Position;      // 阴影盒近端面中心 = 正交近平面
            glm::vec3 half     = sl.HalfExtents;   // 立方体半长 (x=宽, y=高, z=厚度)

            // 光视矩阵: 光位于近端面, 视线方向 = 光传播方向(本地 +Z)。
            // lookAt 把视线映射到视图空间 -z, 故物体在光前方(沿 +Z)落在视图空间负 z。
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::abs(glm::dot(up, lightDir)) > 0.99f)
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, up);

            // 正交投影: 范围盒对齐光空间, x/y = ±half; lookAt 视线 = 光传播方向(+Z),
            // 物体在光前方落在视图空间负 z, z∈[-2half.z, 0] (近面 0 = lightPos, 远面 -2half.z)。
            // 不做 Y 翻转: shadow pass 的 viewport 未用负 height, 渲染与采样统一 v = 0.5 + 0.5*y,
            // 避免双重翻转/区间符号不一致导致 uv 与贴图像素错位 (影子偏小/偏移的根源)。
            glm::mat4 lightProj = glm::ortho(
                -half.x,  half.x,
                -half.y,  half.y,
                 0.0f,2.0f * half.z);
        	lightProj[1][1] *= -1;  // Vulkan Y-flip


            // 渲染用矩阵与采样用矩阵统一为 NDC[-1,1] 的 lightViewProj (不带 bias)。
            // bias(NDC→UV) 的变换改在 MeshShader.frag 的 CalcShadowFactor 里手动完成。
            // 注意: 渲染阴影贴图时同样用此矩阵, 几何完整覆盖贴图, 与采样一致。
            glm::mat4 lightViewProj = lightProj * lightView;
            s_ShadowRenderViewProjs[s] = lightViewProj;
            s_ShadowViewProjs[s]        = lightViewProj;

        }

        UpdateShadowBuffer();
    }

    void Renderer::UpdateShadowBuffer()
    {
        auto context = VulkanContext::Get();
        auto device = context->GetDevice();

        ShadowUBO ubo;
        memset(&ubo, 0, sizeof(ShadowUBO));
        // 默认: 所有光源 shadowMapOfLight = -1 (无阴影)
        for (int i = 0; i < 8; i++)
            ubo.shadowMapOfLight[i] = glm::ivec4(-1, -1, -1, -1);

        for (uint32_t s = 0; s < s_ShadowLightCount; s++)
        {
            ubo.lightViewProj[s] = s_ShadowViewProjs[s];
            ubo.shadowBias[s] = glm::vec4(s_ShadowLights[s].ShadowBias, 0.0f, 0.0f, 0.0f);
            uint32_t li = s_ShadowLights[s].LightIndex;
            if (li < MAX_LIGHTS)
            {
                // 写入 shadowMapOfLight[li] (ivec4 数组, 每个 ivec4 含 4 个 int)
                uint32_t vecIdx = li / 4;
                uint32_t comp = li % 4;
                ubo.shadowMapOfLight[vecIdx][comp] = (int)s;
            }
        }

        VkDeviceSize size = context->GetShadowUBOSize();
        void* data;
        vkMapMemory(device, context->GetShadowUBOMemory(), 0, size, 0, &data);
        memcpy(data, &ubo, sizeof(ShadowUBO));
        vkUnmapMemory(device, context->GetShadowUBOMemory());
    }

    void Renderer::RunShadowPass()
    {
        uint32_t count = s_ShadowLightCount;
        if (count == 0) return;

        auto context = VulkanContext::Get();
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        auto shadowShader = GetShaderLibrary()->Get("Shadow.vert.spv");
        if (!shadowShader) return;

        VkPipelineLayout layout = shadowShader->GetPipelineLayout();

        // 对每个阴影光: 绑对应 framebuffer + 光视矩阵, 画同一份 mesh 队列
        for (uint32_t s = 0; s < count; s++)
        {
            VulkanRenderer::BeginShadowRenderPass(context->GetShadowFramebuffer(s));

            // 通过 shader 绑定 Shadow pipeline (按 RenderPass 名查找)
            shadowShader->Bind("Shadow");
            // Shadow pass 不需要 global/material descriptor set, 仅 push constant
            // (global set 仍可绑, 但 Shadow shader 未引用 set=0 资源, 绑了也无妨;
            //  此处保持与 Mesh pass 一致, 不绑避免校验警告)
            for (auto& request : s_MeshRenderQueue)
            {
                request.Mesh->Bind();

                // push model (offset 0)
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                    sizeof(glm::mat4), &request.Transform);
                // push lightViewProj (offset 64): 渲染用矩阵, 切勿带 bias (见 EndShadowCollection 注释)
                vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                    sizeof(glm::mat4), sizeof(glm::mat4), &s_ShadowRenderViewProjs[s]);

                vkCmdDrawIndexed(cmd, request.SubmeshIndexCount, 1,
                    request.SubmeshFirstIndex, request.SubmeshFirstVertex, 0);
            }

            VulkanRenderer::EndRenderPass();
        }
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

    void Renderer::DrawMeshes()
    {
        if (s_MeshRenderQueue.empty()) return;
    	
    	VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();
    	VkDescriptorSet globalDescriptorSet = VulkanContext::Get()->GetCurrentDescriptorSet();
    	// swapchain/descriptor 重建期间 set 可能为空, 跳过绑定避免 08600 validation 报错
    	if (globalDescriptorSet == VK_NULL_HANDLE)
    		return;
    	
    	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Renderer::GetShaderLibrary()->Get("Mesh.vert.spv")->GetPipelineLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);
    	
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

		// swapchain/descriptor 重建 (如窗口最小化、resize、关闭) 期间,
		// 当前帧的 global descriptor set 可能尚未分配/已失效 (VK_NULL_HANDLE).
		// 此时绑定空 set 会触发 VUID-vkCmdDrawIndexed-None-08600, 故直接跳过本帧天空盒.
		VkDescriptorSet globalSet = ctx->GetCurrentDescriptorSet();
		if (globalSet == VK_NULL_HANDLE)
			return;

		VkCommandBuffer cmd = ctx->GetCurrentCommandBuffer();

		s_SkyboxShader->Bind();

		// Remove translation from view matrix for infinite skybox
		glm::mat4 viewNoTrans = s_SceneData.ViewMatrix;
		viewNoTrans[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 vp = s_SceneData.ProjectionMatrix * viewNoTrans;

		VkPipelineLayout layout = s_SkyboxShader->GetPipelineLayout();
		vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);

		// Bind global descriptor set (set=0) which contains the cubemap at binding 3
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			layout, 0, 1, &globalSet, 0, nullptr);

		s_SkyboxMesh->Bind();
		const auto& sub = s_SkyboxMesh->GetSubmeshes()[0];
		vkCmdDrawIndexed(cmd, sub.IndexCount, 1, sub.FirstIndex, sub.FirstVertex, 0);
	}

	
}
