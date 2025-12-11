#pragma once
#include <glm/glm.hpp>
#include <map>
#include <Engine/Renderer/EditorCamera.h>
#include <Engine/Scene/Components.h>
#include <Engine/Renderer/Shader.h>

namespace Engine {
	struct RenderCommandRequest
	{
		glm::mat4 Transform;
		std::shared_ptr<Mesh> Mesh;
		std::shared_ptr<Material> Material; 
		int EntityID; 

		uint32_t SubmeshIndexCount;
		uint32_t SubmeshFirstIndex;
		uint32_t SubmeshFirstVertex;

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
		static void Shutdown();

		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& camera);
		static void EndScene();

		static void SubmitMesh(const glm::mat4& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<MeshRendererComponent>& rendererComponent, int entityID = -1);
		static void Flush();
		//static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		static API GetAPI() { return s_API; }
		static std::shared_ptr<ShaderLibrary> GetShaderLibrary() { return s_ShaderLibrary; }

		static Renderer* Create();

	private:
		static void DrawMesh(RenderCommandRequest request);

	private:
		static API s_API;

		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
		};
		static std::vector<RenderCommandRequest> s_RenderQueue;
		static std::shared_ptr<ShaderLibrary> s_ShaderLibrary;
		//static Scope<SceneData> s_SceneData;
	};
}