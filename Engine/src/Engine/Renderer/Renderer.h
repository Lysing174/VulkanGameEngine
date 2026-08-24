#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/ModelLibrary.h>
#include <Engine/Renderer/Buffer.h>
#include <Engine/Renderer/FrameBuffer.h>

namespace Engine {

	constexpr uint32_t MAX_LIGHTS = 32;           // 场景最大光源数
	constexpr uint32_t MAX_LIGHTS_PER_OBJECT = 4; // 每物体最多光源数

	constexpr uint32_t MAX_SHADOW_MAPS = 4;       // 阴影贴图数组上限 (descriptor 数组大小)
	constexpr uint32_t SHADOW_MAP_SIZE = 2048;    // 单张阴影贴图尺寸 (固定)

	// GPU 端光源结构 (std430, SSBO 用)
	struct GPULight
	{
		glm::vec4 PositionType;   // xyz = 位置(点光) 或 0(方向光), w = 类型: 0=点光, 1=方向光
		glm::vec4 ColorIntensity; // rgb = 颜色, a = 强度
		glm::vec4 DirectionRange; // xyz = 方向(方向光), w = 范围(点光衰减距离)
	};

	struct MeshRenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<Mesh> Mesh;
		std::shared_ptr<Material> Material;
		int EntityID;

		uint32_t SubmeshIndexCount;
		uint32_t SubmeshFirstIndex;
		uint32_t SubmeshFirstVertex;

		// 每物体光源裁剪结果
		uint32_t LightCount = 0;
		uint32_t LightIndices[MAX_LIGHTS_PER_OBJECT] = {};
	};

	// Shadow data UBO (set=0, binding=5) — std140 布局, CPU/GPU 必须一致
	// 注意: std140 中 ivec4 数组每个元素占 16 字节, mat4 占 64 字节
	struct ShadowUBO
	{
		glm::mat4 lightViewProj[MAX_SHADOW_MAPS];   // 每个阴影光的光视投影矩阵 (已含 [0,1] UV 偏移)
		glm::vec4  shadowBias[MAX_SHADOW_MAPS];      // .x = 阴影偏移 bias
		glm::ivec4 shadowMapOfLight[8];             // 每光源(最多32)对应的阴影图 slot, -1 = 无阴影
	};

	class Renderer
	{
	public:
		enum class API
		{
			None = 0, Vulkan = 1
		};

	public:
		static void Init();
		// static void Shutdown();
		//
		// static void OnWindowResize(uint32_t width, uint32_t height);
		//
		// static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& camera);
		static void RunMeshPass();    // 录制 Mesh Pass (开 pass → 绑 global set → 空则跳过 → skybox → mesh → end pass)
		static void RunShadowPass();  // 录制 Shadow Pass (对每个阴影光: 渲深度到阴影贴图)
		static void RunImGuiPass();   // 录制 ImGui Pass (开 pass → 画 ImGui → end pass)
		static void EndScene();       // 提交命令缓冲并呈现 (RunImGuiPass 之后调用)
		static void OnSwapchainRecreated(); // Swapchain 重建后更新深度纹理 descriptor

		// meshID 为 MeshFilterComponent 的模型资产 UUID；
		// material 来源由 rendererComponent.MaterialID 指定 (可与 mesh 不同模型，自由组合)。
		static void SubmitMesh(const glm::mat4& transform, const UUID& meshID, const MeshRendererComponent& rendererComponent, int entityID = -1);

		// 光照系统：收集 + 上传
		static void BeginLightCollection();
	static void SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range);
	static uint32_t SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
		static void EndLightCollection();                                    // 锁定光源列表并上传 GPU
		static void CullLightsForObject(const glm::vec3& worldPos, uint32_t maxLights,
		                                uint32_t& outCount, uint32_t* outIndices);

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
	static void BeginShadowCollection();
	static void SubmitShadowLight(const glm::vec3& position, const glm::vec3& direction,
								  const glm::vec3& halfExtents, float shadowBias, uint32_t lightIndex);
	static void EndShadowCollection(); // 依据各阴影光的范围盒计算光视矩阵
	static void UpdateShadowBuffer();                          // 上传 ShadowUBO (每帧)
		static uint32_t GetShadowLightCount() { return s_ShadowLightCount; }
		// 渲染阴影贴图用的光视投影 (不含 bias 矩阵, 输出 NDC [-1,1])
		static const glm::mat4& GetShadowRenderViewProj(uint32_t index) { return s_ShadowRenderViewProjs[index]; }
		// 采样阴影贴图用的光视投影 (含 bias 矩阵, 输出 UV [0,1])
		static const glm::mat4& GetShadowViewProj(uint32_t index) { return s_ShadowViewProjs[index]; }
		static float GetShadowBias(uint32_t index) { return s_ShadowLights[index].ShadowBias; }

		static API GetAPI() { return s_API; }
		static ShaderLibrary* GetShaderLibrary() { return s_ShaderLibrary.get(); }
		static ModelLibrary* GetModelLibrary() { return s_ModelLibrary.get(); }
		static void SetOffscreenFramebuffer(const std::shared_ptr<Framebuffer>& fb) { s_OffscreenFramebuffer = fb; }
		static std::shared_ptr<Framebuffer> GetOffscreenFramebuffer() { return s_OffscreenFramebuffer; }

		// Skybox (managed by EditorLayer, rendered in EndScene)
		static void SetSkyboxData(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh);

	private:
		static void DrawSkybox();
		static void DrawMesh(const MeshRenderCommandRequest& request);
		static void DrawMeshes();
		static void CreateLightBuffer();       // 创建光照 SSBO
		static void UpdateLightBuffer();       // 更新光照 SSBO (每帧)

	private:
		static API s_API;
		static std::unique_ptr<ModelLibrary> s_ModelLibrary;
		static std::unique_ptr<ShaderLibrary> s_ShaderLibrary;
		
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
			glm::mat4 ViewMatrix;
			glm::mat4 ProjectionMatrix;
			glm::vec3 CameraPosition;
			glm::vec3 CameraForward;

			// Point light data (collected from PointLightComponent)
			glm::vec3 LightPosition = glm::vec3(5.0f, 10.0f, 5.0f);
			glm::vec3 LightColor = glm::vec3(1.0f, 1.0f, 1.0f);
			float LightIntensity = 20.0f;
		};
		static std::vector<MeshRenderCommandRequest> s_MeshRenderQueue;
		static std::shared_ptr<Framebuffer> s_OffscreenFramebuffer;
		static SceneData s_SceneData;

		// Global Gaussian SSBO: all models' data in one buffer
		// Light SSBO: all scene lights (MAX_LIGHTS), set=0 binding=1
        static VkBuffer s_LightSSBO;
        static VkDeviceMemory s_LightSSBOMemory;
        static std::vector<GPULight> s_SceneLights;   // CPU 端光源列表 (收集阶段用)
        static bool s_LightsDirty;                     // 本帧是否有新光源需要更新 SSBO
        static uint32_t s_SceneLightCount;

        // Skybox
        static std::shared_ptr<Shader> s_SkyboxShader;
        static std::shared_ptr<Mesh>   s_SkyboxMesh;

        // Shadow (方向光阴影贴图)
        static ShadowLight  s_ShadowLights[MAX_SHADOW_MAPS]; // 收集到的投射阴影方向光
        static uint32_t     s_ShadowLightCount;              // 实际数量 (≤ MAX_SHADOW_MAPS)
        static glm::mat4    s_ShadowViewProjs[MAX_SHADOW_MAPS];        // 采样用 (含 bias, UV [0,1])
        static glm::mat4    s_ShadowRenderViewProjs[MAX_SHADOW_MAPS];  // 渲染用 (无 bias, NDC [-1,1])
    };
}
