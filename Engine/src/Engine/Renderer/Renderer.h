#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/AssetLibrary.h>
#include <Engine/Renderer/Buffer.h>
#include <Engine/Renderer/FrameBuffer.h>
#include <Engine/Renderer/GlobalBindings.h>
#include <Platform/Vulkan/VulkanBuffer.h>
#include <Platform/Vulkan/VulkanImage.h>
#include <array>
#include "Engine/Renderer/RendererStats.h"

namespace Engine {
	
	class Renderer
	{
	public:
		enum class API
		{
			None = 0, Vulkan = 1
		};

	public:
		Renderer()=delete;
		static void Init();
		static void Shutdown();
		static void BeginRenderQueueSubmit();
		static void EndRenderQueueSubmit();
		//static void RenderScene(const EditorCamera& camera);
		static void BeginScene(const EditorCamera& camera);
		static void RunMeshPass();    // 录制 Mesh Pass (天空盒 → mesh → end pass)
		static void RunShadowPass(){ShadowRenderer::Run();}  // 录制 Shadow Pass (对每个阴影光: 渲深度到阴影贴图)
		static void RunImGuiPass();   // 录制 ImGui Pass (开 pass → 画 ImGui → end pass)
		static void EndScene();       // 提交命令缓冲并呈现 (RunImGuiPass 之后调用)
		static const RendererStatistics& GetStatistics() { return s_Statistics; }
		static void SetCpuTime(double cpuTimeMs,double cpuWaitGpuTimeMs)
		{
			s_FrameStatistics.CpuTimeMs = cpuTimeMs;
			s_FrameStatistics.CpuWaitGpuTimeMs = cpuWaitGpuTimeMs;
		}

		static void OnSwapchainRecreated(); // Swapchain 重建后通知各子系统

		// meshID 为 MeshFilterComponent 的模型资产 UUID；
		// material 来源由 rendererComponent.MaterialID 指定 (可与 mesh 不同模型，自由组合)。
		static void SubmitMesh(const glm::mat4& transform, const UUID& meshID, const MeshRendererComponent& rendererComponent, int entityID = -1);

		// 光照阴影系统：收集 + 上传
		static void SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range);
		static uint32_t SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
		static void SubmitShadowLight(const glm::vec3& position, const glm::vec3& direction,
										  const glm::vec3& halfExtents, float shadowBias, uint32_t lightIndex);// 锁定光源列表并上传 GPU
		
		static API GetAPI() { return s_API; }
		static ShaderLibrary* GetShaderLibrary() { return s_ShaderLibrary.get(); }
		static AssetLibrary<Model>* GetModelLibrary() { return s_ModelLibrary.get(); }
		static AssetLibrary<GaussianModel>* GetGaussianModelLibrary();
		static void SetOffscreenFramebuffer(const std::shared_ptr<Framebuffer>& fb) { s_OffscreenFramebuffer = fb; }
		static std::shared_ptr<Framebuffer> GetOffscreenFramebuffer() { return s_OffscreenFramebuffer; }

		// Skybox (managed by EditorLayer, rendered in EndScene)
		static void SetSkyboxData(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh);

		// 按 set 序号解析该 set 的 VkDescriptorSetLayout (由拥有该 set 的子系统提供)
		// 0=Gaussian(GaussianRenderer), MATERIAL_SET=shader 自身, LIGHT_SET=ShadowRenderer, ENV_SET=EnvRenderer.
		// material set 由 shader 自身持有, 不经此解析.
		static VkDescriptorSetLayout GetDescriptorSetLayout(uint32_t set);

		// 高斯收集 (转发给 GaussianRenderer)
		static void SubmitGaussian(const glm::mat4& transform, const UUID& gaussianID, int entityID);

		// IBL 环境贴图 (cubemap + SH), 转发给 EnvRenderer (EditorLayer 加载后调用)
		static void SetEnvironmentMap(const VkDescriptorImageInfo& envMapInfo, const std::vector<glm::vec4>& shData);

	private:
		static RendererStatistics s_FrameStatistics;
		static RendererStatistics s_Statistics;
		static API s_API;
		static std::unique_ptr<AssetLibrary<Model>> s_ModelLibrary;
		static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;

		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
			glm::mat4 ViewMatrix;
			glm::mat4 ProjectionMatrix;
			glm::vec3 CameraPosition;
			glm::vec3 CameraForward;

		};
		static SceneData s_SceneData;
		static std::vector<MeshRenderCommandRequest> s_MeshRenderQueue;
		static std::shared_ptr<Framebuffer> s_OffscreenFramebuffer;

		class ShadowRenderer
		{
		public:
			static void Run();
			static void OnSwapchainRecreated();
			static void Init();   // 创建 set2: LightSSBO/ShadowUBO + layout + descriptor set
			static void Exit();   // 销毁 set2 资源
			// 阴影系统：收集 + 计算光视投影矩阵 (tight shadow frustum + 纹素对齐)
			struct ShadowLight
			{
				// 阴影范围盒: 以光为中心, 朝向 = 光本地 +Z (光从原点照向 +Z)
				glm::vec3 Position    = { 0.0f, 0.0f, 0.0f }; // 阴影盒近端中心 (光所在处, 正交近平面)
				glm::vec3 Direction   = { 0.0f, 0.0f, 1.0f }; // 世界空间光照方向 (= 光本地 +Z)
				glm::vec3 HalfExtents = { 10.0f, 10.0f, 10.0f }; // 立方体半长 (x=宽, y=高, z=沿光方向厚度)
				float     ShadowBias  = 0.0015f;               // 阴影偏移
				uint32_t  LightIndex  = 0;                    // 对应的场景方向光在 s_SceneLights 中的下标
			};
			static void BeginLightCollection();
			static void SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range);
			static uint32_t SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
			static void EndLightCollection(); 
			static void BeginShadowCollection();
			static void SubmitShadowLight(const glm::vec3& position, const glm::vec3& direction,
										  const glm::vec3& halfExtents, float shadowBias, uint32_t lightIndex);
			static void EndShadowCollection(); // 依据各阴影光的范围盒计算光视矩阵

			static void CullLightsForObject(const glm::vec3& worldPos, uint32_t maxLights,
										uint32_t& outCount, uint32_t* outIndices);

			
			//static uint32_t GetShadowLightCount() { return s_ShadowLightCount; }
			// 渲染阴影贴图用的光视投影 (不含 bias 矩阵, 输出 NDC [-1,1])
			//static const glm::mat4& GetShadowRenderViewProj(uint32_t index) { return s_ShadowRenderViewProjs[index]; }
			// 采样阴影贴图用的光视投影 (含 bias 矩阵, 输出 UV [0,1])
			//static const glm::mat4& GetShadowViewProj(uint32_t index) { return s_ShadowViewProjs[index]; }
			//static float GetShadowBias(uint32_t index) { return s_ShadowLights[index].ShadowBias; }

			static void SetShadowEnabled(bool enabled) { s_ShadowEnabled = enabled; }
			static bool IsShadowEnabled() { return s_ShadowEnabled; }

			// set2 资源, 供 mesh pass 绑定
			static VkDescriptorSetLayout GetLightDescriptorLayout() { return s_LightDescriptorLayout; }
			static VkDescriptorSet       GetLightDescriptorSet()   { return s_LightDescriptorSet; }
			//static void SetLightDescriptorSet(VkDescriptorSet set) { s_LightDescriptorSet = set; }

		private:
			static void CreateLightBuffer();          // 创建 Light SSBO (set2 b0)
			static void CreateShadowBuffer();         // 创建 Shadow UBO (set2 b1)
			static void CreateShadowResources();	  // 创建阴影贴图资源 (set2 b2)
            static void DestroyShadowResources();	  // 销毁阴影贴图资源 (set2 b2)
			static void CreateDescriptorSetLayout();  // set2 layout: b0 LightSSBO, b1 ShadowUBO, b2 shadowmaps[]
			static void CreateDescriptorSet();        // 分配并写入 set2
			static void UpdateLightBuffer();          // 更新光照 SSBO (每帧)
			static void UpdateShadowBuffer();         // 更新阴影 UBO (每帧)
			
			// GPU 缓冲 (set2 数据, Init 中创建, 与 swapchain 无关)
			static VulkanBuffer s_LightSSBO;   // set2 b0: 全部场景光源 (storage)
			static VulkanBuffer s_ShadowUBO;   // set2 b1: 光视矩阵 + 映射 + bias (uniform)
			static std::vector<GPULight> s_SceneLights;   // CPU 端光源列表 (收集阶段用)
			static bool s_LightsDirty;                     // 本帧是否有新光源需要更新 SSBO
			static uint32_t s_SceneLightCount;

			// set2 descriptor (Init 后有效)
			static VkDescriptorSetLayout s_LightDescriptorLayout;
			static VkDescriptorSet       s_LightDescriptorSet;

			static ShadowLight  s_ShadowLights[MAX_SHADOW_MAPS]; // 收集到的投射阴影方向光
			static uint32_t     s_ShadowLightCount;              // 实际数量 (≤ MAX_SHADOW_MAPS)
			static glm::mat4    s_ShadowViewProjs[MAX_SHADOW_MAPS];        // 采样用 (含 bias, UV [0,1])
			static glm::mat4    s_ShadowRenderViewProjs[MAX_SHADOW_MAPS];  // 渲染用 (无 bias, NDC [-1,1])

			static bool s_ShadowEnabled;   // 是否启用阴影管线

			// 阴影贴图资源 (set2 b2), 由 ShadowRenderer 持有 (renderpass 仍在 VulkanContext)
			static std::array<std::unique_ptr<VulkanImage>, MAX_SHADOW_MAPS> s_ShadowImages;   // 深度图 (含 view)
			static VkSampler s_ShadowSampler;
			static std::array<VkFramebuffer, MAX_SHADOW_MAPS> s_ShadowFramebuffers;
			static VkExtent2D s_ShadowExtent;
			static bool s_ShadowResourcesCreated;
		};

		//只管理set3环境贴图等，没有对应的renderpass
		class EnvResource
		{
		public:
			static void Init();    // 创建 set3: SH 辐照度 UBO + cubemap descriptor set + layout
			static void Exit();    // 销毁 set3 资源
			static void OnSwapchainRecreated();
			// IBL: 设置环境贴图 (预滤波 cubemap) + SH 系数 (EditorLayer 加载完成后调用)
			static void SetEnvironmentMap(const VkDescriptorImageInfo& envMapInfo, const std::vector<glm::vec4>& shData);

			// set3 资源, 供 mesh/skybox pass 绑定
			static VkDescriptorSetLayout GetDescriptorSetLayout() { return s_EnvDescriptorLayout; }
			static VkDescriptorSet       GetDescriptorSet()       { return s_EnvDescriptorSet; }

		private:
			static void CreateDescriptorSetLayout();  // set3: b0 SH UBO, b1 cubemap (combined image sampler)
			static void CreateDescriptorSet();        // 分配并写入 set3 (b0 SH; b1 cubemap 待 SetEnvironmentMap)

			static VulkanBuffer s_SHBuffer;             // set3 b0: SH 辐照度 (9 × vec4)
			static VkDescriptorSetLayout s_EnvDescriptorLayout;
			static VkDescriptorSet       s_EnvDescriptorSet;
			static VkDescriptorImageInfo s_EnvMapInfo;  // 缓存 cubemap (sampler + imageView)
			static bool s_EnvResourcesCreated;
		};

		class MeshRenderer
		{
		public:
			static void Run();    // 录制 Mesh Pass (天空盒 → mesh → end pass)
			static void SubmitMesh(const glm::mat4& transform, const UUID& meshID,
				const MeshRendererComponent& rendererComponent, int entityID = -1);
			static void SetSkyboxData(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh);

		private:
			static void DrawSkybox();
			static void DrawMesh(const MeshRenderCommandRequest& request);
			static void DrawMeshes();
			
			static std::shared_ptr<Shader> s_SkyboxShader;
			static std::shared_ptr<Mesh>   s_SkyboxMesh;
		};

		// 高斯 splat 渲染器: 管理 set1 (FrameInfo UBO + 高斯 SSBO + 排序索引 SSBO + 深度 + model transform SSBO).
		// 高斯 SSBO 长度不固定 (随场景中高斯资产数量变化), 故 init 只创建 DS layout;
		// SSBO / DS 随高斯资源变化 (MarkGaussianDirty) 重建 → 上传 → 绑定.
		class GaussianRenderer
		{
		public:
			static void Init();    // 仅创建 DS layout (set1)
			static void Exit();    // 销毁 DS layout / SSBO / DS
			static void OnSwapchainRecreated();
			static void Run();     // 录制高斯 splat pass (Mesh pass 之后调用)

			// 收集 (每帧, 类似 SubmitMesh) — Scene::RenderEditorScene 调用
			struct GaussianSubmit
			{
				glm::mat4 Transform;
				UUID      GaussianID;
				int       EntityID = -1;
			};
			static void BeginGaussianCollection();
			static void SubmitGaussian(const glm::mat4& transform, const UUID& gaussianID, int entityID);
			static void EndGaussianCollection();       // 对比签名 → 若 dirty 则重建并上传

			// 标记高斯资源变更 (添加/删除/修改高斯组件或携带组件 entity 时调用)
			static void MarkGaussianDirty() { s_GaussiansDirty = true; }

			static AssetLibrary<GaussianModel>* GetModelLibrary() { return s_ModelLibrary.get(); }
			static uint32_t GetTotalGaussianCount() { return s_TotalGaussianCount; }

			// 供高斯 shader 的 pipeline layout 解析 (Renderer::GetDescriptorSetLayout(GAUSSIAN_SET))
			static VkDescriptorSetLayout GetGaussianDescriptorLayout() { return s_GaussianDescriptorLayout; }
			static VkDescriptorSet       GetGaussianDescriptorSet()   { return s_GaussianDescriptorSet; }

		private:
			static void CreateDescriptorSetLayout();  // set0(GAUSSIAN_SET): b0 FrameInfo UBO, b1 Gaussian SSBO, b2 SortedIndex SSBO, b3 DepthMap, b4 ModelTransform SSBO
			static void RecreateGaussianResources();  // (重)建全部 SSBO/UBO + 上传高斯数据 + 分配并写入 DS (b0/b1/b2/b4); 上传必须重建故合并
			static void UpdateModelTransforms();      // 每帧刷新 model transform SSBO (gizmo 移动等)
			static void UpdateFrameInfoUBO();         // 每帧刷新 FrameInfo UBO (splat 参数, 不含相机矩阵)
			static void CreateComputeResources();
			static void InsertBarrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
												  VkPipelineStageFlags dstStage, VkAccessFlags dstAccess);
			static void ComputeSort(VkCommandBuffer cmd);
			
			// GPU 缓冲 (set1)
			static VulkanBuffer s_GaussianSSBO;        // b1: 全部场景高斯 (storage, 变长)
			static VulkanBuffer s_ModelTransformSSBO;  // b4: 每实例 model 变换 (storage, 固定一项/实例)
			static VulkanBuffer s_FrameInfoUBO;        // b0: 帧信息 UBO (固定 192B)
		// compute 基数排序用 ping-pong 缓冲 (compute shader 的 set0)
		static VulkanBuffer s_KeysBuffers[2];        // Dist 写 keys[0]; 每 pass 交换, 末态 keys[0] 为有序键(绘制未直接使用)
		static VulkanBuffer s_ValuesBuffers[2];      // Dist 写 values[0](=恒等索引); 四轮排序后 values[0] 直接供绘制 b2 读取
		static VulkanBuffer s_GlobalHistogramSSBO;   // Upsweep/Spine 全局直方图 (1024 uint = 4pass*256, 每帧 vkCmdFillBuffer 清零)
		static VulkanBuffer s_PartitionHistogramSSBO; // 每 partition(512元素) 直方图 (numPartitions*256 uint)

		// (绘制用相机矩阵经 push constant 传入, 不再单独占用 set/UBO)


		// 4 个排序 compute shader (LoadCompute 加载) + 各自 set / pipeline layout / descriptor set
		static std::shared_ptr<VulkanComputeShader> s_DistShader;
		static std::shared_ptr<VulkanComputeShader> s_UpsweepShader;
		static std::shared_ptr<VulkanComputeShader> s_SpineShader;
		static std::shared_ptr<VulkanComputeShader> s_DownsweepShader;
		static VkDescriptorSetLayout s_DistSetLayout;
		static VkDescriptorSetLayout s_UpsweepSetLayout;
		static VkDescriptorSetLayout s_SpineSetLayout;
		static VkDescriptorSetLayout s_DownsweepSetLayout;
		static VkPipelineLayout s_DistPipelineLayout;
		static VkPipelineLayout s_UpsweepPipelineLayout;
		static VkPipelineLayout s_SpinePipelineLayout;
		static VkPipelineLayout s_DownsweepPipelineLayout;
		static VkDescriptorSet s_DistSet;
		static VkDescriptorSet s_UpsweepSet[2];
		static VkDescriptorSet s_SpineSet;
		static VkDescriptorSet s_DownsweepSet[2];

			static VkDescriptorSetLayout s_GaussianDescriptorLayout;
			static VkDescriptorSet       s_GaussianDescriptorSet;
			static uint32_t s_TotalGaussianCount;
			static bool     s_GaussiansDirty;
			static bool     s_GaussianResourcesCreated;
			static VkSampler s_DepthSampler;           // u_DepthMap 采样器 (运行时绑定, 待 1x depth resolve)

			// 收集阶段数据
			static std::vector<GaussianSubmit> s_GaussianSubmitList;  // 本帧收集到的高斯
			static std::vector<UUID>          s_LastSignature;        // 上次上传的签名 (UUID 列表, 用于检测变化)
			static std::unique_ptr<AssetLibrary<GaussianModel>> s_ModelLibrary;
		};
	};
}
