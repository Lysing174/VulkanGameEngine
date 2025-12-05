#pragma once

#include "Engine/Renderer/Buffer.h"
#include <glm/glm.hpp>
#include <tiny_obj_loader.h>



namespace Engine {
	class RendererAPI
	{
	public:
		enum class API
		{
			None = 0, Vulkan = 1
		};
	public:
		virtual ~RendererAPI() = default;

		virtual void Init() = 0;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;

		//virtual void DrawIndexed(const VertexBuffer vertexArray, uint32_t indexCount = 0) = 0;
		//virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

		//virtual void SetLineWidth(float width) = 0;

		static void LoadModel(std::string modelPath, std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices);


		static API GetAPI() { return s_API; }
		static RendererAPI* Create();
	private:
		static API s_API;

	};

}