#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/Buffer.h>

namespace Engine {

	struct MeshRenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<Mesh> Mesh;
		std::shared_ptr<Material> Material;
		int EntityID;

		uint32_t SubmeshIndexCount;
		uint32_t SubmeshFirstIndex;
		uint32_t SubmeshFirstVertex;

	};

	struct GaussianRenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<GaussianModel> Model;
		int EntityID;
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
		static void SubmitGaussian(const glm::mat4& transform, const std::shared_ptr<GaussianModel>& model, int entityID = -1);

		static API GetAPI() { return s_API; }
		static std::shared_ptr<ShaderLibrary> GetShaderLibrary() { return s_ShaderLibrary; }

		static Renderer* Create();

		// Register a GaussianModel into the global SSBO (assigns offset)
		static void RegisterGaussianModel(const std::shared_ptr<GaussianModel>& model);

	private:
		static void DrawMesh(const MeshRenderCommandRequest& request);
		static void DrawGaussian(const GaussianRenderCommandRequest& request);
		static void FlushMeshPass();
		static void FlushGaussianPass();
		static void RebuildGlobalGaussianSSBO();
		static void CreateGaussianDescriptorSet();

	private:
		static API s_API;

		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
			glm::vec3 CameraPosition;
			glm::vec3 CameraForward;
		};
		static std::vector<MeshRenderCommandRequest> s_MeshRenderQueue;
		static std::vector<GaussianRenderCommandRequest> s_GaussianRenderQueue;
		static std::shared_ptr<ShaderLibrary> s_ShaderLibrary;
		static SceneData s_SceneData;

		// Global Gaussian SSBO: all models' data in one buffer
		static std::shared_ptr<ShaderStorageBuffer> s_GlobalGaussianSSBO;
		static uint32_t s_TotalGaussianCount;
		static bool s_GaussianSSBODirty;
		static std::vector<std::shared_ptr<GaussianModel>> s_RegisteredGaussianModels;

		// Gaussian DescriptorSet (depth texture + global SSBO, bind once per frame)
		static VkDescriptorSet s_GaussianDescriptorSet;
	};
}
