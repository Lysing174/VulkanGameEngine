#pragma once
#include "Engine/Renderer/Renderer.h"

#include <Engine/Renderer/Mesh.h>
#include <glm/glm.hpp>


namespace Engine {
	class VulkanRenderer:public Renderer
	{
	public:
		virtual ~VulkanRenderer() = default;

		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& camera);
		static void EndScene();

		//virtual void Init() = 0;
		//virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
		//virtual void SetClearColor(const glm::vec4& color) = 0;
		//virtual void Clear() = 0;

		static void DrawMesh(RenderCommandRequest request);
		//virtual void DrawIndexed(const VertexBuffer vertexArray, uint32_t indexCount = 0) = 0;
		//virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

		//virtual void SetLineWidth(float width) = 0;

	private:

	};

}