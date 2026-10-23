#include "pch.h"
#include "Engine/Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanRenderer.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanComputeShader.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanFrameBuffer.h"

#include <algorithm>
#include <cfloat>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>


namespace Engine {
    RendererStatistics Renderer::s_FrameStatistics;
    RendererStatistics Renderer::s_Statistics;
    Renderer::API Renderer::s_API = Renderer::API::Vulkan;
	std::vector<MeshRenderCommandRequest> Renderer::s_MeshRenderQueue;
    std::unique_ptr<ShaderLibrary> Renderer::s_ShaderLibrary = std::make_unique<ShaderLibrary>();
    std::unique_ptr<AssetLibrary<Model>> Renderer::s_ModelLibrary = std::make_unique<AssetLibrary<Model>>();
    std::unique_ptr<AssetLibrary<GaussianModel>> Renderer::GaussianRenderer::s_ModelLibrary = std::make_unique<AssetLibrary<GaussianModel>>();

	// Gaussian (set0): FrameInfo UBO + 高斯 SSBO + 排序索引 SSBO + 深度 + model transform SSBO
	VkDescriptorSetLayout Renderer::GaussianRenderer::s_GaussianDescriptorLayout = VK_NULL_HANDLE;
	VkDescriptorSet       Renderer::GaussianRenderer::s_GaussianDescriptorSet = VK_NULL_HANDLE;
	VulkanBuffer         Renderer::GaussianRenderer::s_GaussianSSBO;
	VulkanBuffer         Renderer::GaussianRenderer::s_ModelTransformSSBO;
	VulkanBuffer         Renderer::GaussianRenderer::s_FrameInfoUBO;
	VulkanBuffer         Renderer::GaussianRenderer::s_KeysBuffers[2];
	VulkanBuffer         Renderer::GaussianRenderer::s_ValuesBuffers[2];
	VulkanBuffer         Renderer::GaussianRenderer::s_GlobalHistogramSSBO;
	VulkanBuffer         Renderer::GaussianRenderer::s_PartitionHistogramSSBO;

	std::shared_ptr<VulkanComputeShader> Renderer::GaussianRenderer::s_DistShader = nullptr;
	std::shared_ptr<VulkanComputeShader> Renderer::GaussianRenderer::s_UpsweepShader = nullptr;
	std::shared_ptr<VulkanComputeShader> Renderer::GaussianRenderer::s_SpineShader = nullptr;
	std::shared_ptr<VulkanComputeShader> Renderer::GaussianRenderer::s_DownsweepShader = nullptr;
	VkDescriptorSetLayout Renderer::GaussianRenderer::s_DistSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout Renderer::GaussianRenderer::s_UpsweepSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout Renderer::GaussianRenderer::s_SpineSetLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout Renderer::GaussianRenderer::s_DownsweepSetLayout = VK_NULL_HANDLE;
	VkPipelineLayout      Renderer::GaussianRenderer::s_DistPipelineLayout = VK_NULL_HANDLE;
	VkPipelineLayout      Renderer::GaussianRenderer::s_UpsweepPipelineLayout = VK_NULL_HANDLE;
	VkPipelineLayout      Renderer::GaussianRenderer::s_SpinePipelineLayout = VK_NULL_HANDLE;
	VkPipelineLayout      Renderer::GaussianRenderer::s_DownsweepPipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSet       Renderer::GaussianRenderer::s_DistSet = VK_NULL_HANDLE;
	VkDescriptorSet       Renderer::GaussianRenderer::s_UpsweepSet[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	VkDescriptorSet       Renderer::GaussianRenderer::s_SpineSet = VK_NULL_HANDLE;
	VkDescriptorSet       Renderer::GaussianRenderer::s_DownsweepSet[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	uint32_t             Renderer::GaussianRenderer::s_TotalGaussianCount = 0;
	bool                 Renderer::GaussianRenderer::s_GaussiansDirty = true;
	bool                 Renderer::GaussianRenderer::s_GaussianResourcesCreated = false;
	VkSampler            Renderer::GaussianRenderer::s_DepthSampler = VK_NULL_HANDLE;
	std::vector<Renderer::GaussianRenderer::GaussianSubmit> Renderer::GaussianRenderer::s_GaussianSubmitList;
	std::vector<UUID>    Renderer::GaussianRenderer::s_LastSignature;
    std::shared_ptr<Framebuffer> Renderer::s_OffscreenFramebuffer = nullptr;
    Renderer::SceneData Renderer::s_SceneData;

    // Light SSBO & Shadow UBO (set2 b0 / b1), 由 ShadowRenderer 通过 VulkanBuffer 持有
    VulkanBuffer Renderer::ShadowRenderer::s_LightSSBO;
    VulkanBuffer Renderer::ShadowRenderer::s_ShadowUBO;
    VkDescriptorSetLayout Renderer::ShadowRenderer::s_LightDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet Renderer::ShadowRenderer::s_LightDescriptorSet = VK_NULL_HANDLE;

    // 阴影贴图资源 (set2 b2)
    std::array<std::unique_ptr<VulkanImage>, MAX_SHADOW_MAPS> Renderer::ShadowRenderer::s_ShadowImages;
    VkSampler   Renderer::ShadowRenderer::s_ShadowSampler = VK_NULL_HANDLE;
    std::array<VkFramebuffer, MAX_SHADOW_MAPS> Renderer::ShadowRenderer::s_ShadowFramebuffers = {};
    VkExtent2D  Renderer::ShadowRenderer::s_ShadowExtent = { 2048, 2048 };
    bool        Renderer::ShadowRenderer::s_ShadowResourcesCreated = false;
    std::vector<GPULight> Renderer::ShadowRenderer::s_SceneLights;
    bool Renderer::ShadowRenderer::s_LightsDirty = true;
    uint32_t Renderer::ShadowRenderer::s_SceneLightCount = 0;

	// Env (set3): SH 辐照度 + 预滤波环境 cubemap
	VulkanBuffer Renderer::EnvResource::s_SHBuffer;
	VkDescriptorSetLayout Renderer::EnvResource::s_EnvDescriptorLayout = VK_NULL_HANDLE;
	VkDescriptorSet Renderer::EnvResource::s_EnvDescriptorSet = VK_NULL_HANDLE;
	VkDescriptorImageInfo Renderer::EnvResource::s_EnvMapInfo = {};
	bool Renderer::EnvResource::s_EnvResourcesCreated = false;

    // Skybox (MeshRenderer)
    std::shared_ptr<Shader> Renderer::MeshRenderer::s_SkyboxShader = nullptr;
    std::shared_ptr<Mesh>   Renderer::MeshRenderer::s_SkyboxMesh   = nullptr;

    // Shadow
    Renderer::ShadowRenderer::ShadowLight Renderer::ShadowRenderer::s_ShadowLights[MAX_SHADOW_MAPS];
    uint32_t              Renderer::ShadowRenderer::s_ShadowLightCount = 0;
    bool                 Renderer::ShadowRenderer::s_ShadowEnabled = true;
    glm::mat4             Renderer::ShadowRenderer::s_ShadowViewProjs[MAX_SHADOW_MAPS];        // 采样用 (含 bias)
    glm::mat4             Renderer::ShadowRenderer::s_ShadowRenderViewProjs[MAX_SHADOW_MAPS];  // 渲染用 (无 bias)

	// ============================================================
    // Init
    // ============================================================
    void Renderer::Init()
    {
        // set2 所有权转交 ShadowRenderer (创建 LightSSBO/ShadowUBO + layout + descriptor set)
        ShadowRenderer::Init();
        // set3 (Env: SH 辐照度 + 预滤波 cubemap) 由 EnvRenderer 持有
        EnvResource::Init();
    	// 高斯渲染器: 仅创建 DS layout (SSBO/DS 按高斯资源变化按需重建)
    	GaussianRenderer::Init();
        
        // Mesh Shader: PBR
        s_ShaderLibrary->Load("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", ShaderConfig::DefaultPBR());
        // Skybox Shader
        s_ShaderLibrary->Load("shaders/Skybox.vert.spv", "shaders/Skybox.frag.spv", ShaderConfig::Skybox());
        // Shadow Shader (方向光阴影贴图生成 pass)
        s_ShaderLibrary->Load("shaders/Shadow.vert.spv", "shaders/Shadow.frag.spv", ShaderConfig::Shadow());
    	
    	s_ShaderLibrary->Load("shaders/GaussianSplat.vert.spv","shaders/GaussianSplat.frag.spv",ShaderConfig::GaussianSplat());
    	
    }

    

    // ============================================================
    // OnSwapchainRecreated
    // ============================================================
    AssetLibrary<GaussianModel>* Renderer::GetGaussianModelLibrary()
	{
		return GaussianRenderer::GetModelLibrary();
	}

	void Renderer::OnSwapchainRecreated()
    {
        // 重新缓存 light SSBO 句柄 (swapchain 重建时 VulkanContext 会重建 buffer)
        ShadowRenderer::OnSwapchainRecreated();
        EnvResource::OnSwapchainRecreated();
    }

    // ============================================================
    // Scene lifecycle
    // ============================================================
	void Renderer::BeginRenderQueueSubmit()
    {
        s_FrameStatistics = {};
    	s_MeshRenderQueue.clear();
    	ShadowRenderer::BeginShadowCollection();
    	ShadowRenderer::BeginLightCollection();
    	GaussianRenderer::BeginGaussianCollection();
    }

	void Renderer::EndRenderQueueSubmit()
	{
    	ShadowRenderer::EndLightCollection();
    	ShadowRenderer::EndShadowCollection();
    	// 高斯: 收集完毕, 若签名变化(高斯资产集合变更)则重建并上传 SSBO
    	GaussianRenderer::EndGaussianCollection();

		s_FrameStatistics.SceneGaussianCount = GaussianRenderer::GetTotalGaussianCount();
		s_FrameStatistics.GaussianDataBytes = s_FrameStatistics.SceneGaussianCount * sizeof(GaussianDataGPU);
		// TODO(culling): 改为剔除后实际提交绘制的高斯和顶点数量，暂用全场景数量。
		s_FrameStatistics.DrawnGaussianCount = s_FrameStatistics.SceneGaussianCount;
		s_FrameStatistics.DrawnVertexCount = s_FrameStatistics.SceneVertexCount;
	}
	void Renderer::BeginScene(const EditorCamera& camera)
	{
        s_SceneData.CameraPosition = camera.GetPosition();
        s_SceneData.CameraForward  = camera.GetForwardDirection();
        s_SceneData.ViewMatrix     = camera.GetViewMatrix();
        s_SceneData.ProjectionMatrix = camera.GetProjection();
        s_SceneData.ProjectionMatrix[1][1] *= -1; // Vulkan Y-flip

    	glm::mat4 projView = s_SceneData.ProjectionMatrix * s_SceneData.ViewMatrix;
    	VulkanContext::Get()->BeginFrame(projView);//此处从query pool读取上一帧gpu耗时
    	// 注意: set0 为高斯专属, 此处不再绑定全局 set; mesh/skybox 在各自 pass 内绑定 set1/2/3.
	}

	void Renderer::RunMeshPass()
	{
		auto* context = VulkanContext::Get();
		context->BeginGpuTiming(RenderStage::Mesh);
		MeshRenderer::Run();
		context->EndGpuTiming(RenderStage::Mesh);
		// 高斯 splat 合成 pass: 在 offscreen FBO 上 LOAD, 在 mesh 结果之上合成 (shader 加载 / 1x depth 就绪前为 no-op)
		GaussianRenderer::Run();
	}

	void Renderer::MeshRenderer::Run()
	{
    	VulkanContext::Get()->BeginMeshRenderPass(std::static_pointer_cast<VulkanFramebuffer>(s_OffscreenFramebuffer));

		// Draw skybox first (behind all meshes, depth write off)
		DrawSkybox();

		DrawMeshes();
		VulkanRenderer::EndRenderPass();  // Close mesh RP
	}

	void Renderer::RunImGuiPass()
	{
    	VulkanRenderer::BeginUIRenderPass();  // Open ImGui RP (LOAD, not CLEAR)
    	VulkanRenderer::DrawImGui();           // Draw ImGui in its own RP
    	VulkanRenderer::EndRenderPass();        // Close ImGui RP
	}

	void Renderer::EndScene()
	{
    	VulkanContext::Get()->EndFrame();
    	    	
    	s_FrameStatistics.GPU = VulkanContext::Get()->GetGpuTimings();
    	s_FrameStatistics.HasCompletedFrame = true;
    	s_Statistics = s_FrameStatistics;
	}

    void Renderer::SubmitMesh(const glm::mat4& transform, const UUID& meshID, const MeshRendererComponent& rendererComponent, int entityID)
	{
		MeshRenderer::SubmitMesh(transform, meshID, rendererComponent, entityID);
	}

	void Renderer::MeshRenderer::SubmitMesh(const glm::mat4& transform, const UUID& meshID, const MeshRendererComponent& rendererComponent, int entityID)
    {
        // 由 Renderer 从 AssetLibrary<Model> 按 UUID 解析出 Mesh 指针
        auto meshModel = Renderer::GetModelLibrary()->Load(meshID);
        auto mesh = meshModel ? meshModel->GetMesh() : nullptr;
        if (!mesh) { return; }
		s_FrameStatistics.SceneVertexCount += mesh->GetVertexCount();

        // 材质来源 model (可能与 mesh 不同，支持跨模型自由组合)
        auto matModel = Renderer::GetModelLibrary()->Load(rendererComponent.MaterialID);

        // 从模型矩阵获取世界位置
        glm::vec3 worldPos = glm::vec3(transform[3]);

        // CPU 光源裁剪: 选出最近的 N 个光源
        uint32_t lightCount = 0;
        uint32_t lightIndices[MAX_LIGHTS_PER_OBJECT] = {};
        ShadowRenderer::CullLightsForObject(worldPos, rendererComponent.MaxLights, lightCount, lightIndices);

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
    void Renderer::ShadowRenderer::CreateLightBuffer()
    {
        // Light SSBO (set2 b0): 全部场景光源
        s_LightSSBO = VulkanBuffer(MAX_LIGHTS * sizeof(GPULight),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void Renderer::ShadowRenderer::CreateShadowBuffer()
    {
        // Shadow UBO (set2 b1): 光视矩阵 + 映射 + bias
        s_ShadowUBO = VulkanBuffer(sizeof(ShadowUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void Renderer::ShadowRenderer::UpdateLightBuffer()
    {
        if (!s_LightsDirty) return;

        // 填充到固定大小以匹配 SSBO
        std::vector<GPULight> gpuLights(MAX_LIGHTS);
        memset(gpuLights.data(), 0, MAX_LIGHTS * sizeof(GPULight));
        uint32_t count = std::min(s_SceneLightCount, MAX_LIGHTS);
        memcpy(gpuLights.data(), s_SceneLights.data(), count * sizeof(GPULight));

        s_LightSSBO.SetData(gpuLights.data(), MAX_LIGHTS * sizeof(GPULight));

        s_LightsDirty = false;
    }

    void Renderer::ShadowRenderer::BeginLightCollection()
    {
        s_SceneLights.clear();
        s_LightsDirty = true;
    }
	
	void Renderer::SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range)
    {
		++s_FrameStatistics.PointLightCount;
    	ShadowRenderer::SubmitPointLight(position, color, intensity, range);
    }

	void Renderer::SubmitGaussian(const glm::mat4& transform, const UUID& gaussianID, int entityID)
	{
		GaussianRenderer::SubmitGaussian(transform, gaussianID, entityID);
	}
    void Renderer::ShadowRenderer::SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range)
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
		++s_FrameStatistics.DirectLightCount;
    	return ShadowRenderer::SubmitDirectLight(direction, color, intensity);
    }

	void Renderer::SubmitShadowLight(const glm::vec3& position, const glm::vec3& direction,
		const glm::vec3& halfExtents, float shadowBias, uint32_t lightIndex)
	{
    	ShadowRenderer::SubmitShadowLight(position, direction, halfExtents, shadowBias, lightIndex);
	}
	uint32_t Renderer::ShadowRenderer::SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity)
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

    void Renderer::ShadowRenderer::EndLightCollection()
    {
        s_SceneLightCount = (uint32_t)s_SceneLights.size();
        UpdateLightBuffer();
    }

    // ============================================================
    // Shadow System
    // ============================================================
    void Renderer::ShadowRenderer::BeginShadowCollection()
    {
        s_ShadowLightCount = 0;
    }

    void Renderer::ShadowRenderer::SubmitShadowLight(const glm::vec3& position, const glm::vec3& direction,
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
    void Renderer::ShadowRenderer::EndShadowCollection()
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

    void Renderer::ShadowRenderer::UpdateShadowBuffer()
    {
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

        s_ShadowUBO.SetData(&ubo, sizeof(ShadowUBO));
    }

    void Renderer::ShadowRenderer::Run()
    {
        if (!s_ShadowEnabled) return;
        uint32_t count = s_ShadowLightCount;
        if (count == 0) return;

        auto context = VulkanContext::Get();
        VkCommandBuffer cmd = context->GetCurrentCommandBuffer();
        auto shadowShader = GetShaderLibrary()->Get("Shadow.vert.spv");
        if (!shadowShader) return;

        context->BeginGpuTiming(RenderStage::Shadow);

        VkPipelineLayout layout = shadowShader->GetPipelineLayout();

        // 通过 shader 绑定 Shadow pipeline (按 RenderPass 名查找)
        shadowShader->Bind("Shadow");
        // 绑定 light set (局部序号, Shadow shader 的 pipeline layout 含该 set 需绑定以通过校验)
        {
            if (s_LightDescriptorSet != VK_NULL_HANDLE)
            {
                int lightLocal = shadowShader->GetConfig().GetLocalSetIndex(LIGHT_SET);
                if (lightLocal >= 0)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)lightLocal, 1, &s_LightDescriptorSet, 0, nullptr);
            }
        }
        // 对每个阴影光: 绑对应 framebuffer + 光视矩阵, 画同一份 mesh 队列
        for (uint32_t s = 0; s < count; s++)
        {
            VulkanRenderer::BeginShadowRenderPass(s_ShadowFramebuffers[s], s_ShadowExtent);
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
        context->EndGpuTiming(RenderStage::Shadow);
    }

    void Renderer::ShadowRenderer::OnSwapchainRecreated()
    {
    	// set2 (LightSSBO / ShadowUBO / 阴影图) 与 swapchain 无关, 无需重建
    
    }

    void Renderer::Shutdown()
    {
        ShadowRenderer::Exit();
        EnvResource::Exit();
        GaussianRenderer::Exit();
        s_OffscreenFramebuffer.reset();
    }

    VkDescriptorSetLayout Renderer::GetDescriptorSetLayout(uint32_t set)
    {
        switch (set)
        {
            case GAUSSIAN_SET: // 高斯资源 (GLSL set0) — GaussianRenderer
                return GaussianRenderer::GetGaussianDescriptorLayout();
            case LIGHT_SET: // 光源 SSBO + 阴影 UBO + 阴影贴图 — ShadowRenderer
                return ShadowRenderer::GetLightDescriptorLayout();
            case ENV_SET:    // SH IBL 辐照度 + 预滤波环境 cubemap — EnvRenderer
                return EnvResource::GetDescriptorSetLayout();
            default:
                EG_CORE_WARN("Renderer::GetDescriptorSetLayout - 未注册 set={0} 的 descriptor set layout", set);
                return VK_NULL_HANDLE;
        }
    }

    void Renderer::SetEnvironmentMap(const VkDescriptorImageInfo& envMapInfo, const std::vector<glm::vec4>& shData)
    {
        EnvResource::SetEnvironmentMap(envMapInfo, shData);
    }

    // ============================================================
    // EnvRenderer (set3: SH 辐照度 + 预滤波环境 cubemap)
    // ============================================================
    void Renderer::EnvResource::Init()
    {
        CreateDescriptorSetLayout();
        CreateDescriptorSet();
    }

    void Renderer::EnvResource::Exit()
    {
        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();
        if (s_EnvDescriptorSet != VK_NULL_HANDLE)
            vkFreeDescriptorSets(device, context->GetDescriptorPool(), 1, &s_EnvDescriptorSet);
        if (s_EnvDescriptorLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, s_EnvDescriptorLayout, nullptr);
        s_SHBuffer.Destroy();
        s_EnvDescriptorSet = VK_NULL_HANDLE;
        s_EnvDescriptorLayout = VK_NULL_HANDLE;
        s_EnvResourcesCreated = false;
    }

    void Renderer::EnvResource::OnSwapchainRecreated()
    {
        // set3 与 swapchain 无关 (descriptor pool 在 resize 时不被销毁), 无需重建
    }

    void Renderer::EnvResource::SetEnvironmentMap(const VkDescriptorImageInfo& envMapInfo, const std::vector<glm::vec4>& shData)
    {
        s_EnvMapInfo = envMapInfo;

        // 写入 SH 系数到 UBO (shared across frames)
        if (s_SHBuffer.IsValid() && !shData.empty())
            s_SHBuffer.SetData(shData.data(), shData.size() * sizeof(glm::vec4));

        // 写入 cubemap 到 set3 b1 (descriptor set 或 cubemap 尚未就绪时跳过)
        if (s_EnvDescriptorSet == VK_NULL_HANDLE) return;
        if (s_EnvMapInfo.imageView == VK_NULL_HANDLE) return;
        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = s_EnvDescriptorSet;
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &s_EnvMapInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void Renderer::EnvResource::CreateDescriptorSetLayout()
    {
        // set3: b0 SH 辐照度 UBO (9 × vec4), b1 预滤波环境 cubemap (combined image sampler)
        std::array<VkDescriptorSetLayoutBinding, 2> bindings = {};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(VulkanContext::Get()->GetDevice(), &layoutInfo, nullptr, &s_EnvDescriptorLayout) != VK_SUCCESS)
            throw std::runtime_error("failed to create env descriptor set layout!");
    }

    void Renderer::EnvResource::CreateDescriptorSet()
    {
        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();

        // SH buffer (9 × vec4) 持久存在, 仅创建一次
        if (!s_SHBuffer.IsValid())
            s_SHBuffer = VulkanBuffer(9 * sizeof(glm::vec4),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &s_EnvDescriptorLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &s_EnvDescriptorSet) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate env descriptor set!");

        s_EnvResourcesCreated = true;

        // b0: SH 辐照度 UBO (b1 cubemap 在 SetEnvironmentMap 中于贴图加载后写入)
        VkDescriptorBufferInfo shInfo = {};
        shInfo.buffer = s_SHBuffer.GetBuffer();
        shInfo.offset = 0;
        shInfo.range = s_SHBuffer.GetSize();
        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = s_EnvDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &shInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    void Renderer::ShadowRenderer::Init()
    {
        CreateLightBuffer();           // s_LightSSBO (VulkanBuffer)
        CreateShadowBuffer();          // s_ShadowUBO (VulkanBuffer)
        CreateShadowResources();       // 阴影贴图 image/view/framebuffer/sampler (set2 b2)
        CreateDescriptorSetLayout();   // s_LightDescriptorLayout (set2)
        CreateDescriptorSet();         // s_LightDescriptorSet (绑定 b0/b1/b2, b2 需先有贴图)
    }

    void Renderer::ShadowRenderer::Exit()
    {
        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();

        if (s_LightDescriptorSet != VK_NULL_HANDLE)
            vkFreeDescriptorSets(device, context->GetDescriptorPool(), 1, &s_LightDescriptorSet);
        if (s_LightDescriptorLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, s_LightDescriptorLayout, nullptr);

        // set2 buffer 由 VulkanBuffer RAII 管理, 显式 Destroy 释放 Vk 资源
        s_LightSSBO.Destroy();
        s_ShadowUBO.Destroy();

        // 阴影贴图资源 (image/view/framebuffer/sampler) 释放
        DestroyShadowResources();
    }

    void Renderer::ShadowRenderer::CreateDescriptorSetLayout()
    {
        // set2: b0 LightSSBO (storage), b1 ShadowUBO (uniform), b2 shadow maps (combined image sampler 数组)
        std::array<VkDescriptorSetLayoutBinding, 3> bindings = {};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = MAX_SHADOW_MAPS;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(VulkanContext::Get()->GetDevice(), &layoutInfo, nullptr, &s_LightDescriptorLayout) != VK_SUCCESS)
            throw std::runtime_error("failed to create light descriptor set layout!");
    }

    void Renderer::ShadowRenderer::CreateDescriptorSet()
    {
        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &s_LightDescriptorLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &s_LightDescriptorSet) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate light descriptor set!");

        std::array<VkWriteDescriptorSet, 3> writes = {};

        // b0: Light SSBO
        VkDescriptorBufferInfo lightInfo = {};
        lightInfo.buffer = s_LightSSBO.GetBuffer();
        lightInfo.offset = 0;
        lightInfo.range = s_LightSSBO.GetSize();
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = s_LightDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &lightInfo;

        // b1: Shadow UBO
        VkDescriptorBufferInfo shadowUBOInfo = {};
        shadowUBOInfo.buffer = s_ShadowUBO.GetBuffer();
        shadowUBOInfo.offset = 0;
        shadowUBOInfo.range = s_ShadowUBO.GetSize();
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = s_LightDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &shadowUBOInfo;

        // b2: shadow maps (数组, 大小 = MAX_SHADOW_MAPS), 由 ShadowRenderer 自有资源 (已迁至此)
        std::array<VkDescriptorImageInfo, MAX_SHADOW_MAPS> shadowImageInfos = {};
        for (uint32_t s = 0; s < MAX_SHADOW_MAPS; s++)
        {
            shadowImageInfos[s].sampler = s_ShadowSampler;
            shadowImageInfos[s].imageView = s_ShadowImages[s]->GetImageView();
            shadowImageInfos[s].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = s_LightDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = MAX_SHADOW_MAPS;
        writes[2].pImageInfo = shadowImageInfos.data();

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void Renderer::ShadowRenderer::CreateShadowResources()
    {
        if (s_ShadowResourcesCreated) return;
        s_ShadowResourcesCreated = true;

        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();
        VkFormat shadowFormat = VK_FORMAT_D32_SFLOAT;

        // --- sampler ---
        {
            VkSamplerCreateInfo samplerInfo = {};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 1.0f;
            samplerInfo.compareEnable = VK_FALSE;  // 硬阴影, 后续 PCF 再开
            samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
            samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;

            if (vkCreateSampler(device, &samplerInfo, nullptr, &s_ShadowSampler) != VK_SUCCESS)
                throw std::runtime_error("failed to create shadow sampler!");
        }

        // --- 深度图 + view + framebuffer (renderpass 来自 VulkanContext, 本步不迁移) ---
        VkRenderPass shadowRP = context->GetShadowRenderPass()->GetVkRenderPass();
        for (uint32_t s = 0; s < MAX_SHADOW_MAPS; s++)
        {
            VulkanImageSpec spec;
            spec.Width  = s_ShadowExtent.width;
            spec.Height = s_ShadowExtent.height;
            spec.MipLevels = 1;
            spec.ArrayLayers = 1;
            spec.Format = shadowFormat;
            spec.Tiling = VK_IMAGE_TILING_OPTIMAL;
            spec.Usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            spec.MemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            spec.AspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
            spec.ViewType = VK_IMAGE_VIEW_TYPE_2D;
            s_ShadowImages[s] = std::make_unique<VulkanImage>(spec);

            // Every descriptor slot must be readable, including maps with no active light.
            // The shadow render pass transitions active maps to its writable subpass layout.
            s_ShadowImages[s]->TransitionLayout(
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            VkImageView view = s_ShadowImages[s]->GetImageView();
            VkFramebufferCreateInfo fbInfo = {};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = shadowRP;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &view;
            fbInfo.width = s_ShadowExtent.width;
            fbInfo.height = s_ShadowExtent.height;
            fbInfo.layers = 1;
            if (vkCreateFramebuffer(device, &fbInfo, nullptr, &s_ShadowFramebuffers[s]) != VK_SUCCESS)
                throw std::runtime_error("failed to create shadow framebuffer!");
        }
    }

    void Renderer::ShadowRenderer::DestroyShadowResources()
    {
        if (!s_ShadowResourcesCreated) return;
        auto* context = VulkanContext::Get();
        VkDevice device = context->GetDevice();

        for (uint32_t s = 0; s < MAX_SHADOW_MAPS; s++)
        {
            if (s_ShadowFramebuffers[s] != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, s_ShadowFramebuffers[s], nullptr);
            s_ShadowFramebuffers[s] = VK_NULL_HANDLE;
        }
        if (s_ShadowSampler != VK_NULL_HANDLE)
            vkDestroySampler(device, s_ShadowSampler, nullptr);
        s_ShadowSampler = VK_NULL_HANDLE;

        // VulkanImage 析构会自动销毁 image/view/memory
        for (uint32_t s = 0; s < MAX_SHADOW_MAPS; s++)
            s_ShadowImages[s].reset();

        s_ShadowResourcesCreated = false;
    }

    void Renderer::ShadowRenderer::CullLightsForObject(const glm::vec3& worldPos, uint32_t maxLights,
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

    void Renderer::MeshRenderer::DrawMeshes()
    {
        if (s_MeshRenderQueue.empty()) return;
    	
    	VkCommandBuffer cmd = VulkanContext::Get()->GetCurrentCommandBuffer();

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

                // 该 shader 共享的 light/env set 在此 pipeline layout 上按“局部序号”绑定一次
                const ShaderConfig& cfg = lastShader->GetConfig();
                VkPipelineLayout layout = lastShader->GetPipelineLayout();
                VkDescriptorSet lightSet = ShadowRenderer::GetLightDescriptorSet();
                VkDescriptorSet envSet   = EnvResource::GetDescriptorSet();
                int lightLocal = cfg.GetLocalSetIndex(LIGHT_SET);
                int envLocal   = cfg.GetLocalSetIndex(ENV_SET);
                if (lightLocal >= 0 && lightSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)lightLocal, 1, &lightSet, 0, nullptr);
                if (envLocal >= 0 && envSet != VK_NULL_HANDLE)
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)envLocal, 1, &envSet, 0, nullptr);

                // 相机 push constant: projView (offset 112) + cameraPosition (offset 176).
                {
                    glm::mat4 projView = s_SceneData.ProjectionMatrix * s_SceneData.ViewMatrix;
                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        112, sizeof(glm::mat4), &projView);

                    glm::vec4 camPos = glm::vec4(s_SceneData.CameraPosition, 1.0f);
                    vkCmdPushConstants(cmd, layout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        176, sizeof(glm::vec4), &camPos);
                }
            }

            if (command.Material != lastMaterial)
            {
                lastMaterial = command.Material;
                lastMaterial->Bind();   // 绑定 material descriptor set (set1)
            }
            DrawMesh(command);
        }
    }
	
	void Renderer::MeshRenderer::DrawMesh(const MeshRenderCommandRequest& request)
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
	MeshRenderer::SetSkyboxData(shader, mesh);
}

	// ============================================================
	// GaussianRenderer (set0: FrameInfo UBO + 高斯 SSBO + 排序索引 + 深度 + model transform)
	// 高斯 SSBO 长度随场景高斯资产数量变化, 故 init 只建 DS layout; SSBO/DS 按需重建.
	// ============================================================
	void Renderer::GaussianRenderer::Init()
	{
		CreateDescriptorSetLayout();

		// 创建深度采样器: 高斯 pass 通过 b3(u_DepthMap) 采样 mesh 的 1x depth resolve 做遮挡.
		// 普通 (非 comparison) 采样器, shader 内手动比较深度; NEAREST 避免插值深度.
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter               = VK_FILTER_NEAREST;
		samplerInfo.minFilter               = VK_FILTER_NEAREST;
		samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable        = VK_FALSE;
		samplerInfo.compareEnable           = VK_FALSE;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.minLod                  = 0.0f;
		samplerInfo.maxLod                  = 1.0f;
		if (vkCreateSampler(VulkanContext::Get()->GetDevice(), &samplerInfo, nullptr, &s_DepthSampler) != VK_SUCCESS)
			throw std::runtime_error("failed to create gaussian depth sampler!");

		// 加载排序 compute shader + 绘制 shader; compute 资源在此创建, 绘制 pipeline 由 EditorLayer 按 config 驱动创建
		CreateComputeResources();
	}

	void Renderer::GaussianRenderer::Exit()
	{
		auto* context = VulkanContext::Get();
		VkDevice device = context->GetDevice();

		if (s_GaussianDescriptorSet != VK_NULL_HANDLE)
			vkFreeDescriptorSets(device, context->GetDescriptorPool(), 1, &s_GaussianDescriptorSet);
		s_GaussianDescriptorSet = VK_NULL_HANDLE;

		if (s_GaussianDescriptorLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(device, s_GaussianDescriptorLayout, nullptr);
		s_GaussianDescriptorLayout = VK_NULL_HANDLE;

		s_GaussianSSBO.Destroy();
		s_ModelTransformSSBO.Destroy();
		s_FrameInfoUBO.Destroy();
		s_KeysBuffers[0].Destroy(); s_KeysBuffers[1].Destroy();
		s_ValuesBuffers[0].Destroy(); s_ValuesBuffers[1].Destroy();
		s_GlobalHistogramSSBO.Destroy();
		s_PartitionHistogramSSBO.Destroy();

		if (s_DepthSampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, s_DepthSampler, nullptr);
			s_DepthSampler = VK_NULL_HANDLE;
		}

		// 释放 compute / 绘制 资源 (compute shader 句柄由 shared_ptr 析构时自行销毁 pipeline)
		std::array<VkPipelineLayout, 4> pls = { s_DistPipelineLayout, s_UpsweepPipelineLayout,
			s_SpinePipelineLayout, s_DownsweepPipelineLayout };
		for (auto pl : pls) if (pl != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pl, nullptr);
		s_DistPipelineLayout = s_UpsweepPipelineLayout = s_SpinePipelineLayout =
			s_DownsweepPipelineLayout = VK_NULL_HANDLE;

		std::array<VkDescriptorSetLayout, 4> sls = { s_DistSetLayout, s_UpsweepSetLayout,
			s_SpineSetLayout, s_DownsweepSetLayout };
		for (auto l : sls) if (l != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, l, nullptr);
		s_DistSetLayout = s_UpsweepSetLayout = s_SpineSetLayout =
			s_DownsweepSetLayout = VK_NULL_HANDLE;

		

		s_GlobalHistogramSSBO.Destroy();
		s_PartitionHistogramSSBO.Destroy();
		s_KeysBuffers[0].Destroy(); s_KeysBuffers[1].Destroy();
		s_ValuesBuffers[0].Destroy(); s_ValuesBuffers[1].Destroy();

		s_GaussianResourcesCreated = false;
		s_GaussiansDirty = true;
	}

	void Renderer::GaussianRenderer::OnSwapchainRecreated()
	{
		// 高斯 SSBO/DS 与 swapchain 无关 (尺寸按高斯数, 不按图像数), 无需重建;
		// FrameInfo UBO 的视口尺寸在 UpdateFrameInfoUBO 每帧刷新. 故此处为 no-op.
	}

	// ---- 收集 (每帧) ----
	void Renderer::GaussianRenderer::BeginGaussianCollection()
	{
		s_GaussianSubmitList.clear();
	}

	void Renderer::GaussianRenderer::SubmitGaussian(const glm::mat4& transform, const UUID& gaussianID, int entityID)
	{
		if ((uint64_t)gaussianID == 0) return;
		s_GaussianSubmitList.push_back({ transform, gaussianID, entityID });
	}

	//void Renderer::GaussianRenderer::MarkGaussianDirty() { s_GaussiansDirty = true; }

	// ---- 变更检测: 用 UUID 集合签名, 比 Scene 逐实体追踪更简单 ----
	void Renderer::GaussianRenderer::EndGaussianCollection()
	{
		// 计算签名 (高斯资产 UUID 列表) 以检测集合变化 (增/删/改高斯资产)
		std::vector<UUID> signature;
		signature.reserve(s_GaussianSubmitList.size());
		for (const auto& sub : s_GaussianSubmitList)
			signature.push_back(sub.GaussianID);

		bool signatureChanged = (signature.size() != s_LastSignature.size());
		if (!signatureChanged)
		{
			for (size_t i = 0; i < signature.size(); i++)
			{
				if (signature[i] != s_LastSignature[i]) { signatureChanged = true; break; }
			}
		}
		if (signatureChanged) s_GaussiansDirty = true;

		if (s_GaussiansDirty)
		{
			RecreateGaussianResources();
			s_LastSignature = std::move(signature);
			s_GaussiansDirty = false;
		}
	}

	void Renderer::GaussianRenderer::CreateDescriptorSetLayout()
	{
		// set0: b0 FrameInfo UBO, b1 Gaussian SSBO, b2 SortedIndex SSBO, b3 u_DepthMap, b4 ModelTransform SSBO
		std::array<VkDescriptorSetLayoutBinding, 5> bindings = {};

		bindings[0].binding = 0; bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		bindings[1].binding = 1; bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[1].descriptorCount = 1; bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		bindings[1].pImmutableSamplers = nullptr;

		bindings[2].binding = 2; bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[2].descriptorCount = 1; bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		bindings[2].pImmutableSamplers = nullptr;

		bindings[3].binding = 3; bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[3].descriptorCount = 1; bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[3].pImmutableSamplers = nullptr;

		bindings[4].binding = 4; bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[4].descriptorCount = 1; bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		bindings[4].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(VulkanContext::Get()->GetDevice(), &layoutInfo, nullptr, &s_GaussianDescriptorLayout) != VK_SUCCESS)
			throw std::runtime_error("failed to create gaussian descriptor set layout!");
	}

	void Renderer::GaussianRenderer::RecreateGaussianResources()
	{
		auto* context = VulkanContext::Get();
		VkDevice device = context->GetDevice();
		if (vkDeviceWaitIdle(device) != VK_SUCCESS)
			throw std::runtime_error("failed to wait before recreating gaussian resources!");

		// 统计总高斯数 (按各资产 GetGaussianCount)
		s_TotalGaussianCount = 0;
		for (const auto& sub : s_GaussianSubmitList)
		{
			auto model = GetModelLibrary()->Load(sub.GaussianID);
			if (model) s_TotalGaussianCount += model->GetGaussianCount();
		}

		// 释放旧的 DS / 缓冲 (DS 由 descriptor pool 分配, 可单独 free)
		if (s_GaussianDescriptorSet != VK_NULL_HANDLE)
		{
			vkFreeDescriptorSets(device, context->GetDescriptorPool(), 1, &s_GaussianDescriptorSet);
			s_GaussianDescriptorSet = VK_NULL_HANDLE;
		}
		s_GaussianSSBO.Destroy();
		s_ModelTransformSSBO.Destroy();
		s_KeysBuffers[0].Destroy(); s_KeysBuffers[1].Destroy();
		s_ValuesBuffers[0].Destroy(); s_ValuesBuffers[1].Destroy();
		s_GlobalHistogramSSBO.Destroy();
		s_PartitionHistogramSSBO.Destroy();
		if (!s_FrameInfoUBO.IsValid())
			s_FrameInfoUBO = VulkanBuffer(64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (s_TotalGaussianCount == 0)
		{
			UpdateModelTransforms();  // 无高斯: SSBO 无效, 内部 no-op (与合并前 UploadGaussianData 一致)
			s_GaussianResourcesCreated = true;  // 无高斯, 不分配 SSBO
			return;
		}

		VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		s_GaussianSSBO       = VulkanBuffer(s_TotalGaussianCount * sizeof(GaussianDataGPU), storageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		s_ModelTransformSSBO = VulkanBuffer(s_GaussianSubmitList.size() * sizeof(glm::mat4), storageUsage,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		// ping-pong keys/values: values[0] 接收初始索引上传, 四轮排序后直接供绘制读取
		s_KeysBuffers[0]     = VulkanBuffer(s_TotalGaussianCount * sizeof(uint32_t), storageUsage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		s_KeysBuffers[1]     = VulkanBuffer(s_TotalGaussianCount * sizeof(uint32_t), storageUsage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		s_ValuesBuffers[0]   = VulkanBuffer(s_TotalGaussianCount * sizeof(uint32_t), storageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		s_ValuesBuffers[1]   = VulkanBuffer(s_TotalGaussianCount * sizeof(uint32_t), storageUsage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		// 全局直方图: 每帧 vkCmdFillBuffer 清零, 需 TRANSFER_DST
		s_GlobalHistogramSSBO = VulkanBuffer(1024 * sizeof(uint32_t), storageUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		uint32_t numPartitions = (s_TotalGaussianCount + 511) / 512;
		s_PartitionHistogramSSBO = VulkanBuffer(numPartitions * 256 * sizeof(uint32_t), storageUsage,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// 分配 DS (set0)
		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = context->GetDescriptorPool();
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &s_GaussianDescriptorLayout;

		if (vkAllocateDescriptorSets(device, &allocInfo, &s_GaussianDescriptorSet) != VK_SUCCESS)
			throw std::runtime_error("failed to allocate gaussian descriptor set!");

		// 写 b0(FrameInfo UBO) / b1(Gaussian) / b2(SortedIndex) / b4(ModelTransform)
		std::array<VkWriteDescriptorSet, 4> writes = {};

		VkDescriptorBufferInfo uboInfo{};
		uboInfo.buffer = s_FrameInfoUBO.GetBuffer(); uboInfo.offset = 0; uboInfo.range = s_FrameInfoUBO.GetSize();
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet = s_GaussianDescriptorSet;
		writes[0].dstBinding = 0; writes[0].dstArrayElement = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &uboInfo;

		VkDescriptorBufferInfo gaussianInfo{};
		gaussianInfo.buffer = s_GaussianSSBO.GetBuffer(); gaussianInfo.offset = 0; gaussianInfo.range = s_GaussianSSBO.GetSize();
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet = s_GaussianDescriptorSet;
		writes[1].dstBinding = 1; writes[1].dstArrayElement = 0;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].descriptorCount = 1;
		writes[1].pBufferInfo = &gaussianInfo;

		VkDescriptorBufferInfo sortedInfo{};
		sortedInfo.buffer = s_ValuesBuffers[0].GetBuffer(); sortedInfo.offset = 0; sortedInfo.range = s_ValuesBuffers[0].GetSize();
		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[2].dstSet = s_GaussianDescriptorSet;
		writes[2].dstBinding = 2; writes[2].dstArrayElement = 0;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[2].descriptorCount = 1;
		writes[2].pBufferInfo = &sortedInfo;

		VkDescriptorBufferInfo modelInfo{};
		modelInfo.buffer = s_ModelTransformSSBO.GetBuffer(); modelInfo.offset = 0; modelInfo.range = s_ModelTransformSSBO.GetSize();
		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[3].dstSet = s_GaussianDescriptorSet;
		writes[3].dstBinding = 4; writes[3].dstArrayElement = 0;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[3].descriptorCount = 1;
		writes[3].pBufferInfo = &modelInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		// ---- 写 compute 排序 descriptor sets (缓冲句柄已重建, 需重新绑定) ----
		auto writeStorage = [&](VkDescriptorSet set, uint32_t binding, const VulkanBuffer& buf) {
			if (set == VK_NULL_HANDLE || !buf.IsValid()) return;
			VkDescriptorBufferInfo info{};
			info.buffer = buf.GetBuffer(); info.offset = 0; info.range = buf.GetSize();
			VkWriteDescriptorSet w{};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = set; w.dstBinding = binding;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.descriptorCount = 1; w.pBufferInfo = &info;
			vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
		};
		// Dist: b0 GaussianData, b1 keys[0], b2 values[0], b3 ModelTransform
		writeStorage(s_DistSet, 0, s_GaussianSSBO);
		writeStorage(s_DistSet, 1, s_KeysBuffers[0]);
		writeStorage(s_DistSet, 2, s_ValuesBuffers[0]);
		writeStorage(s_DistSet, 3, s_ModelTransformSSBO);
		// Upsweep[src=0/1]: b0 keys[src], b1 GlobalHist, b2 PartitionHist
		writeStorage(s_UpsweepSet[0], 0, s_KeysBuffers[0]);
		writeStorage(s_UpsweepSet[1], 0, s_KeysBuffers[1]);
		writeStorage(s_UpsweepSet[0], 1, s_GlobalHistogramSSBO);
		writeStorage(s_UpsweepSet[1], 1, s_GlobalHistogramSSBO);
		writeStorage(s_UpsweepSet[0], 2, s_PartitionHistogramSSBO);
		writeStorage(s_UpsweepSet[1], 2, s_PartitionHistogramSSBO);
		// Spine: b1 GlobalHist, b2 PartitionHist
		writeStorage(s_SpineSet, 1, s_GlobalHistogramSSBO);
		writeStorage(s_SpineSet, 2, s_PartitionHistogramSSBO);
		// Downsweep[src=0]: b0 keys[0], b1 values[0], b2 PartitionHist, b3 keys[1], b4 values[1]
		writeStorage(s_DownsweepSet[0], 0, s_KeysBuffers[0]);
		writeStorage(s_DownsweepSet[0], 1, s_ValuesBuffers[0]);
		writeStorage(s_DownsweepSet[0], 2, s_PartitionHistogramSSBO);
		writeStorage(s_DownsweepSet[0], 3, s_KeysBuffers[1]);
		writeStorage(s_DownsweepSet[0], 4, s_ValuesBuffers[1]);
		// Downsweep[src=1]: b0 keys[1], b1 values[1], b2 PartitionHist, b3 keys[0], b4 values[0]
		writeStorage(s_DownsweepSet[1], 0, s_KeysBuffers[1]);
		writeStorage(s_DownsweepSet[1], 1, s_ValuesBuffers[1]);
		writeStorage(s_DownsweepSet[1], 2, s_PartitionHistogramSSBO);
		writeStorage(s_DownsweepSet[1], 3, s_KeysBuffers[0]);
		writeStorage(s_DownsweepSet[1], 4, s_ValuesBuffers[0]);

		// ===== 上传高斯数据 + 初始化排序索引 (合并自原 UploadGaussianData: 重建必伴随上传) =====
		// 拼接所有高斯资产 GPU 数据 (modelIndex = 该 submission 在列表中的下标)
		std::vector<GaussianDataGPU> gpu;
		gpu.reserve(s_TotalGaussianCount);
		for (uint32_t i = 0; i < s_GaussianSubmitList.size(); i++)
		{
			const auto& sub = s_GaussianSubmitList[i];
			auto model = GetModelLibrary()->Get(sub.GaussianID);
			if (!model) continue;
			auto gpuData = model->GetGPUData(i);  // 内部已设 modelIndex = i
			for (auto& g : gpuData) gpu.push_back(g);
		}
		if (gpu.size() > s_TotalGaussianCount) gpu.resize(s_TotalGaussianCount);
		s_GaussianSSBO.SetDataStaged(gpu.data(), gpu.size() * sizeof(GaussianDataGPU));

		// 排序索引初始化为恒等 (compute 基数排序前的安全回退)
		std::vector<uint32_t> identity(s_TotalGaussianCount);
		for (uint32_t i = 0; i < s_TotalGaussianCount; i++) identity[i] = i;
		s_ValuesBuffers[0].SetDataStaged(identity.data(), identity.size() * sizeof(uint32_t));

		UpdateModelTransforms();

		// b3(u_DepthMap) 运行时在 Run() 中按当前 offscreen FBO 的 1x depth resolve 实时绑定 (见 b3 遮挡).
		s_GaussianResourcesCreated = true;
	}

	// 每帧刷新 model transform (gizmo 移动等, 不触发 SSBO 重建)
	void Renderer::GaussianRenderer::UpdateModelTransforms()
	{
		if (!s_ModelTransformSSBO.IsValid()) return;
		std::vector<glm::mat4> transforms;
		transforms.reserve(s_GaussianSubmitList.size());
		for (const auto& sub : s_GaussianSubmitList)
			transforms.push_back(sub.Transform);
		if (!transforms.empty())
			s_ModelTransformSSBO.SetData(transforms.data(), transforms.size() * sizeof(glm::mat4));
	}

	// 每帧刷新 FrameInfo UBO (相机相关, 固定 192B, std140)
	void Renderer::GaussianRenderer::UpdateFrameInfoUBO()
	{
		if (!s_FrameInfoUBO.IsValid()) return;

		struct FrameInfoData
		{
			glm::vec4 cameraPosAndScale;  // xyz = camera position, w = splatScale
			glm::vec4 focal;              // xy = focal length (像素)
			glm::vec4 viewportInfo;       // xy = viewport size, zw = 1/size
			glm::vec4 extraInfo;          // x = alphaCullThreshold
		};
		static_assert(sizeof(FrameInfoData) == 64, "FrameInfoData must be 64 bytes (std140)");

		FrameInfoData data{};
		data.cameraPosAndScale = glm::vec4(s_SceneData.CameraPosition, 1.0f); // w = splatScale (TODO: 暴露为可调参数)

		float width  = (float)(s_OffscreenFramebuffer ? s_OffscreenFramebuffer->GetWidth()  : 1);
		float height = (float)(s_OffscreenFramebuffer ? s_OffscreenFramebuffer->GetHeight() : 1);
		// focal (像素): fx = proj[0][0] * 0.5 * width, fy = proj[1][1] * 0.5 * height (投影已 Y-flip)
		data.focal = glm::vec4(s_SceneData.ProjectionMatrix[0][0] * 0.5f * width,
			s_SceneData.ProjectionMatrix[1][1] * 0.5f * height, 0.0f, 0.0f);
		data.viewportInfo = glm::vec4(width, height, 1.0f / width, 1.0f / height);
		data.extraInfo    = glm::vec4(0.001f, 0.0f, 0.0f, 0.0f); // x = alphaCullThreshold

		s_FrameInfoUBO.SetData(&data, sizeof(FrameInfoData));
	}

	void Renderer::GaussianRenderer::CreateComputeResources()
	{
		auto* context = VulkanContext::Get();
		VkDevice device = context->GetDevice();

		// ---- 加载 4 个排序 compute shader (区别于 VS/FS, 走 ShaderLibrary::LoadCompute) ----
		s_DistShader      = GetShaderLibrary()->LoadCompute("shaders/GaussianDist.comp.spv", ShaderConfig{});
		s_UpsweepShader   = GetShaderLibrary()->LoadCompute("shaders/GaussianSortUpsweep.comp.spv", ShaderConfig{});
		s_SpineShader     = GetShaderLibrary()->LoadCompute("shaders/GaussianSortSpine.comp.spv", ShaderConfig{});
		s_DownsweepShader = GetShaderLibrary()->LoadCompute("shaders/GaussianSortDownsweep.comp.spv", ShaderConfig{});

		// ---- compute set0 descriptor set layouts (全部为 storage buffer, COMPUTE stage) ----
		auto makeStorageLayout = [&](const std::vector<uint32_t>& bindings) {
			std::vector<VkDescriptorSetLayoutBinding> lbs;
			lbs.reserve(bindings.size());
			for (uint32_t b : bindings) {
				VkDescriptorSetLayoutBinding lb{};
				lb.binding = b;
				lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				lb.descriptorCount = 1;
				lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
				lbs.push_back(lb);
			}
			VkDescriptorSetLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			info.bindingCount = static_cast<uint32_t>(lbs.size());
			info.pBindings = lbs.data();
			VkDescriptorSetLayout layout = VK_NULL_HANDLE;
			if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
				throw std::runtime_error("failed to create gaussian compute descriptor set layout!");
			return layout;
		};
		s_DistSetLayout      = makeStorageLayout({ 0, 1, 2, 3 });
		s_UpsweepSetLayout   = makeStorageLayout({ 0, 1, 2 });
		s_SpineSetLayout     = makeStorageLayout({ 1, 2 });
		s_DownsweepSetLayout = makeStorageLayout({ 0, 1, 2, 3, 4 });

		// ---- pipeline layouts (各 shader 独立 push constant 范围) ----
		auto makePipelineLayout = [&](VkDescriptorSetLayout setLayout, uint32_t pcSize) {
			VkPushConstantRange pc{};
			pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pc.offset = 0;
			pc.size = pcSize;
			VkPipelineLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			info.setLayoutCount = 1;
			info.pSetLayouts = &setLayout;
			info.pushConstantRangeCount = 1;
			info.pPushConstantRanges = &pc;
			VkPipelineLayout layout = VK_NULL_HANDLE;
			if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
				throw std::runtime_error("failed to create gaussian compute pipeline layout!");
			return layout;
		};
		// Dist: view+projection (2 mat4 = 128B); 其余: 4 uint (16B)
		s_DistPipelineLayout      = makePipelineLayout(s_DistSetLayout, 128);
		s_UpsweepPipelineLayout   = makePipelineLayout(s_UpsweepSetLayout, 16);
		s_SpinePipelineLayout     = makePipelineLayout(s_SpineSetLayout, 16);
		s_DownsweepPipelineLayout = makePipelineLayout(s_DownsweepSetLayout, 16);

		// ---- compute pipelines ----
		s_DistShader->CreatePipeline(s_DistPipelineLayout);
		s_UpsweepShader->CreatePipeline(s_UpsweepPipelineLayout);
		s_SpineShader->CreatePipeline(s_SpinePipelineLayout);
		s_DownsweepShader->CreatePipeline(s_DownsweepPipelineLayout);

		// ---- 分配 descriptor sets (缓冲区句柄在 RecreateGaussianResources 中按帧集合填充) ----
		auto allocSet = [&](VkDescriptorSetLayout layout) {
			VkDescriptorSetAllocateInfo ai{};
			ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			ai.descriptorPool = context->GetDescriptorPool();
			ai.descriptorSetCount = 1;
			ai.pSetLayouts = &layout;
			VkDescriptorSet set = VK_NULL_HANDLE;
			if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS)
				throw std::runtime_error("failed to allocate gaussian compute descriptor set!");
			return set;
		};
		s_DistSet = allocSet(s_DistSetLayout);
		s_UpsweepSet[0] = allocSet(s_UpsweepSetLayout);
		s_UpsweepSet[1] = allocSet(s_UpsweepSetLayout);
		s_SpineSet = allocSet(s_SpineSetLayout);
		s_DownsweepSet[0] = allocSet(s_DownsweepSetLayout);
		s_DownsweepSet[1] = allocSet(s_DownsweepSetLayout);
	}



	void Renderer::GaussianRenderer::InsertBarrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
												  VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
	{
		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = dstAccess;
		vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	void Renderer::GaussianRenderer::ComputeSort(VkCommandBuffer cmd)
	{
		if (s_TotalGaussianCount == 0) return;
		InsertBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		// 每帧清零全局直方图 (4 个 pass 各写其独立 256 区间, 互不重叠)
		vkCmdFillBuffer(cmd, s_GlobalHistogramSSBO.GetBuffer(), 0, s_GlobalHistogramSSBO.GetSize(), 0);
		InsertBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		// ---- Dist: 由高斯数据 + model transform 计算深度键 + 初始(恒等)索引 ----
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DistShader->GetPipeline());
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DistPipelineLayout, 0, 1, &s_DistSet, 0, nullptr);
		struct DistPC { glm::mat4 view; glm::mat4 proj; } distPC{ s_SceneData.ViewMatrix, s_SceneData.ProjectionMatrix };
		vkCmdPushConstants(cmd, s_DistPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DistPC), &distPC);
		vkCmdDispatch(cmd, (s_TotalGaussianCount + 255) / 256, 1, 1);
		InsertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		uint32_t numPartitions = (s_TotalGaussianCount + 511) / 512;
		uint32_t src = 0;
		for (uint32_t pass = 0; pass < 4; ++pass)
		{
			// Upsweep: 统计各 partition 各 digit 计数 + 累加全局直方图
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_UpsweepShader->GetPipeline());
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_UpsweepPipelineLayout, 0, 1, &s_UpsweepSet[src], 0, nullptr);
			uint32_t upPC[4] = { pass, s_TotalGaussianCount, 0, 0 };
			vkCmdPushConstants(cmd, s_UpsweepPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(upPC), upPC);
			vkCmdDispatch(cmd, numPartitions, 1, 1);
			InsertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			// Spine: 全局直方图 exclusive prefix sum + 各 partition 偏移
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_SpineShader->GetPipeline());
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_SpinePipelineLayout, 0, 1, &s_SpineSet, 0, nullptr);
			uint32_t spinePC[4] = { pass, s_TotalGaussianCount, numPartitions, 0 };
			vkCmdPushConstants(cmd, s_SpinePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(spinePC), spinePC);
			vkCmdDispatch(cmd, 1, 1, 1);
			InsertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			// Downsweep: 确定性 scatter (稳定排序, 避免闪烁)
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DownsweepShader->GetPipeline());
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DownsweepPipelineLayout, 0, 1, &s_DownsweepSet[src], 0, nullptr);
			uint32_t downPC[4] = { pass, s_TotalGaussianCount, numPartitions, 0 };
			vkCmdPushConstants(cmd, s_DownsweepPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(downPC), downPC);
			vkCmdDispatch(cmd, numPartitions, 1, 1);
			InsertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

			src = 1 - src;
		}

		// 经 4 次交换, src 回到 0: vertex shader 直接读取 values[0]
		InsertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						  VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	void Renderer::GaussianRenderer::Run()
	{
		if (s_GaussianDescriptorSet == VK_NULL_HANDLE) return;
		if (s_TotalGaussianCount == 0) return;

		auto* context = VulkanContext::Get();
		VkDevice device = context->GetDevice();
		VkCommandBuffer cmd = context->GetCurrentCommandBuffer();

		// 每帧刷新 splat 参数 UBO (不含相机矩阵; 相机矩阵经 push constant 传入)
		UpdateFrameInfoUBO();
		UpdateModelTransforms();

		// 1) GPU 基数排序: 计算深度键并稳定排序, 结果保留在 s_ValuesBuffers[0] (render pass 之外执行)
		context->BeginGpuTiming(RenderStage::GaussianSort);
		ComputeSort(cmd);
		context->EndGpuTiming(RenderStage::GaussianSort);

		auto gaussianShader = GetShaderLibrary()->Get("GaussianSplat.vert.spv");
		//VkPipeline pipeline = gaussianShader->GetPipeline("Gaussian");
		//if (pipeline == VK_NULL_HANDLE) return;
		VkPipelineLayout layout = gaussianShader->GetPipelineLayout();
		const ShaderConfig& cfg = gaussianShader->GetConfig();

		// 相机矩阵 (view + proj) 经 push constant 传入
		struct CameraPC { glm::mat4 view; glm::mat4 proj; } camPC{ s_SceneData.ViewMatrix, s_SceneData.ProjectionMatrix };
		vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CameraPC), &camPC);

		auto offscreenFB = std::static_pointer_cast<VulkanFramebuffer>(s_OffscreenFramebuffer);
		
    	// b3(u_DepthMap) 遮挡: 每帧按当前 offscreen FBO 的 1x depth resolve 实时绑定.
		VkImageView gaussianDepthView = offscreenFB->GetGaussianDepthView();
    	if (s_DepthSampler != VK_NULL_HANDLE && gaussianDepthView != VK_NULL_HANDLE)
    	{
    		VkDescriptorImageInfo depthInfo{};
    		depthInfo.sampler     = s_DepthSampler;
    		depthInfo.imageView   = gaussianDepthView;
    		depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    		VkWriteDescriptorSet depthWrite{};
    		depthWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    		depthWrite.dstSet          = s_GaussianDescriptorSet;
    		depthWrite.dstBinding      = 3;
    		depthWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    		depthWrite.descriptorCount = 1;
    		depthWrite.pImageInfo      = &depthInfo;
    		vkUpdateDescriptorSets(device, 1, &depthWrite, 0, nullptr);
    	}
    	else
    	{
    		EG_CORE_WARN("GaussianRenderer::Run - 深度采样器或 1x 深度视图未就绪, 跳过遮挡测试");
    	}
    	
		context->BeginGpuTiming(RenderStage::Gaussian);
    	context->BeginGaussianRenderPass(offscreenFB);

		//vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    	gaussianShader->Bind();
    	
		// 绑定高斯资源 set (GAUSSIAN_SET 在本 shader pipeline layout 中的局部序号)
		int gaussianSetLocal = cfg.GetLocalSetIndex(GAUSSIAN_SET);
		if (gaussianSetLocal >= 0)
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, (uint32_t)gaussianSetLocal, 1, &s_GaussianDescriptorSet, 0, nullptr);



		// 6 顶点 (一个 quad) / 实例 = 总高斯数; 实例顺序由排序后的 s_ValuesBuffers[0] 决定
		vkCmdDraw(cmd, 6, s_TotalGaussianCount, 0, 0);

		VulkanRenderer::EndRenderPass();
		context->EndGpuTiming(RenderStage::Gaussian);
	}

void Renderer::MeshRenderer::SetSkyboxData(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh)
	{
		s_SkyboxShader = shader;
		s_SkyboxMesh   = mesh;
	}

	void Renderer::MeshRenderer::DrawSkybox()
	{
		if (!s_SkyboxShader || !s_SkyboxMesh)
			return;

		auto* ctx = VulkanContext::Get();
		VkCommandBuffer cmd = ctx->GetCurrentCommandBuffer();

		// env(set3) 在 cubemap 加载完成前为空; 此时跳过天空盒避免绑定空 set 触发校验错误
		VkDescriptorSet envSet = EnvResource::GetDescriptorSet();
		if (envSet == VK_NULL_HANDLE)
			return;

    	GetShaderLibrary()->Get("Skybox.vert.spv")->Bind();
		s_SkyboxShader->Bind();

		// Remove translation from view matrix for infinite skybox
		glm::mat4 viewNoTrans = s_SceneData.ViewMatrix;
		viewNoTrans[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 vp = s_SceneData.ProjectionMatrix * viewNoTrans;

		VkPipelineLayout layout = s_SkyboxShader->GetPipelineLayout();
		vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &vp);

		// Bind env descriptor set (局部序号; 全局编号见 GlobalBindings::ENV_SET)
		int envLocal = s_SkyboxShader->GetConfig().GetLocalSetIndex(ENV_SET);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			layout, (uint32_t)envLocal, 1, &envSet, 0, nullptr);

		s_SkyboxMesh->Bind();
		const auto& sub = s_SkyboxMesh->GetSubmeshes()[0];
		vkCmdDrawIndexed(cmd, sub.IndexCount, 1, sub.FirstIndex, sub.FirstVertex, 0);
	}

	
}
