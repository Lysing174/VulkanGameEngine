#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Buffer.h>
#include <Engine/Renderer/FrameBuffer.h>

namespace Engine {

	constexpr uint32_t MAX_LIGHTS = 32;           // 场景最大光源数
	constexpr uint32_t MAX_LIGHTS_PER_OBJECT = 4; // 每物体最多光源数

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
		static void EndScene();       // 录制场景命令 (Mesh + Gaussian)，不提交
		static void PresentFrame();   // 绘制 ImGui + 提交命令缓冲 + 呈现
		static void OnSwapchainRecreated(); // Swapchain 重建后更新深度纹理 descriptor

		static void SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID = -1);

		// 光照系统：收集 + 上传
		static void BeginLightCollection();
		static void SubmitPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float range);
		static void SubmitDirectLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
		static void EndLightCollection();                                    // 锁定光源列表并上传 GPU
		static void CullLightsForObject(const glm::vec3& worldPos, uint32_t maxLights,
		                                uint32_t& outCount, uint32_t* outIndices);

		static API GetAPI() { return s_API; }
		static std::shared_ptr<ShaderLibrary> GetShaderLibrary() { return s_ShaderLibrary; }
		static void SetOffscreenFramebuffer(const std::shared_ptr<Framebuffer>& fb) { s_OffscreenFramebuffer = fb; }
		static std::shared_ptr<Framebuffer> GetOffscreenFramebuffer() { return s_OffscreenFramebuffer; }

		// Skybox (managed by EditorLayer, rendered in EndScene)
		static void SetSkyboxData(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh);

	private:
		static void DrawSkybox();
		static void DrawMesh(const MeshRenderCommandRequest& request);
		static void FlushMeshPass();
		static void CreateLightBuffer();       // 创建光照 SSBO
		static void UpdateLightBuffer();       // 更新光照 SSBO (每帧)

	private:
		static API s_API;

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
		static std::shared_ptr<ShaderLibrary> s_ShaderLibrary;
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
    };
}
