#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>

namespace Engine {

	class GaussianSplat;

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
		glm::vec2 RectOffset = {0.0f, 0.0f}; // 矩形起始偏移 (屏幕空间 [0,1])
		glm::vec2 RectScale  = {0.3f, 0.3f}; // 矩形尺寸 (宽, 高)
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
		static void SubmitGaussian(const glm::vec2& rectOffset, const glm::vec2& rectScale, int entityID = -1);
		// static void Flush();
		//static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		static API GetAPI() { return s_API; }
		static std::shared_ptr<ShaderLibrary> GetShaderLibrary() { return s_ShaderLibrary; }

		static Renderer* Create();

	private:
		static void DrawMesh(const MeshRenderCommandRequest& request);
		static void DrawGaussian(const GaussianRenderCommandRequest& request);
		static void FlushMeshPass();
		static void FlushGaussianPass();
		static void CreateGaussianDescriptorSet();

	private:
		static API s_API;

		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};
		static std::vector<MeshRenderCommandRequest> s_MeshRenderQueue;
		static std::vector<GaussianRenderCommandRequest> s_GaussianRenderQueue;
		static std::shared_ptr<ShaderLibrary> s_ShaderLibrary;

		// Gaussian 深度纹理 DescriptorSet (直接绑定深度，不走 Material)
		static VkDescriptorSet s_GaussianDescriptorSet;
		//static Scope<SceneData> s_SceneData;
	};
}